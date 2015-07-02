/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2015 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * In addition, as a special exception, the copyright holders of this
 * program give permission to link the code of its release with the
 * OpenSSL project's "OpenSSL" library (or with modified versions of it
 * that use the same license as the "OpenSSL" library), and distribute
 * the linked executables. You must obey the GNU General Public License
 * in all respects for all of the code used other than "OpenSSL". If you
 * modify file(s) with this exception, you may extend this exception to
 * your version of the file(s), but you are not obligated to do so. If
 * you do not wish to do so, delete this exception statement from your
 * version. If you delete this exception statement from all source files
 * in the program, then also delete it here.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#include "PrecompiledHeadersServer.h"
#include "ServerContext.h"

#include "../Core/HttpServer/FilesystemHttpSender.h"
#include "FromDcmtkBridge.h"
#include "ServerToolbox.h"
#include "OrthancInitialization.h"

#include <glog/logging.h>
#include <EmbeddedResources.h>
#include <dcmtk/dcmdata/dcfilefo.h>


#include "Scheduler/CallSystemCommand.h"
#include "Scheduler/DeleteInstanceCommand.h"
#include "Scheduler/ModifyInstanceCommand.h"
#include "Scheduler/StoreScuCommand.h"
#include "Scheduler/StorePeerCommand.h"
#include "OrthancRestApi/OrthancRestApi.h"
#include "../Plugins/Engine/OrthancPlugins.h"


#define ENABLE_DICOM_CACHE  1

static const size_t DICOM_CACHE_SIZE = 2;

/**
 * IMPORTANT: We make the assumption that the same instance of
 * FileStorage can be accessed from multiple threads. This seems OK
 * since the filesystem implements the required locking mechanisms,
 * but maybe a read-writer lock on the "FileStorage" could be
 * useful. Conversely, "ServerIndex" already implements mutex-based
 * locking.
 **/

namespace Orthanc
{
  void ServerContext::ChangeThread(ServerContext* that)
  {
    while (!that->done_)
    {
      std::auto_ptr<IDynamicObject> obj(that->pendingChanges_.Dequeue(100));
        
      if (obj.get() != NULL)
      {
        const ServerIndexChange& change = dynamic_cast<const ServerIndexChange&>(*obj.get());

        boost::recursive_mutex::scoped_lock lock(that->listenersMutex_);
        for (ServerListeners::iterator it = that->listeners_.begin(); 
             it != that->listeners_.end(); ++it)
        {
          try
          {
            it->GetListener().SignalChange(change);
          }
          catch (OrthancException& e)
          {
            LOG(ERROR) << "Error in the " << it->GetDescription() 
                       << " callback while signaling a change: " << e.What();
          }
        }
      }
    }
  }


  ServerContext::ServerContext(IDatabaseWrapper& database) :
    index_(*this, database),
    compressionEnabled_(false),
    provider_(*this),
    dicomCache_(provider_, DICOM_CACHE_SIZE),
    scheduler_(Configuration::GetGlobalIntegerParameter("LimitJobs", 10)),
    lua_(*this),
    plugins_(NULL),
    done_(false),
    queryRetrieveArchive_(Configuration::GetGlobalIntegerParameter("QueryRetrieveSize", 10)),
    defaultLocalAet_(Configuration::GetGlobalStringParameter("DicomAet", "ORTHANC"))
  {
    uint64_t s = Configuration::GetGlobalIntegerParameter("DicomAssociationCloseDelay", 5);  // In seconds
    scu_.SetMillisecondsBeforeClose(s * 1000);  // Milliseconds are expected here

    listeners_.push_back(ServerListener(lua_, "Lua"));

    changeThread_ = boost::thread(ChangeThread, this);
  }


  
  ServerContext::~ServerContext()
  {
    if (!done_)
    {
      LOG(ERROR) << "INTERNAL ERROR: ServerContext::Stop() should be invoked manually to avoid mess in the destruction order!";
      Stop();
    }
  }


  void ServerContext::Stop()
  {
    if (!done_)
    {
      {
        boost::recursive_mutex::scoped_lock lock(listenersMutex_);
        listeners_.clear();
      }

      done_ = true;

      if (changeThread_.joinable())
      {
        changeThread_.join();
      }

      scu_.Finalize();

      // Do not change the order below!
      scheduler_.Stop();
      index_.Stop();
    }
  }


  void ServerContext::SetCompressionEnabled(bool enabled)
  {
    if (enabled)
      LOG(WARNING) << "Disk compression is enabled";
    else
      LOG(WARNING) << "Disk compression is disabled";

    compressionEnabled_ = enabled;
  }

  void ServerContext::RemoveFile(const std::string& fileUuid,
                                 FileContentType type)
  {
    accessor_.Remove(fileUuid, type);
  }


  StoreStatus ServerContext::Store(std::string& resultPublicId,
                                   DicomInstanceToStore& dicom)
  {
    try
    {
      DicomInstanceHasher hasher(dicom.GetSummary());
      resultPublicId = hasher.HashInstance();

      Json::Value simplified;
      SimplifyTags(simplified, dicom.GetJson());

      // Test if the instance must be filtered out
      bool accepted = true;

      {
        boost::recursive_mutex::scoped_lock lock(listenersMutex_);

        for (ServerListeners::iterator it = listeners_.begin(); it != listeners_.end(); ++it)
        {
          try
          {
            if (!it->GetListener().FilterIncomingInstance(simplified, dicom.GetRemoteAet()))
            {
              accepted = false;
              break;
            }
          }
          catch (OrthancException& e)
          {
            LOG(ERROR) << "Error in the " << it->GetDescription() 
                       << " callback while receiving an instance: " << e.What();
            throw;
          }
        }
      }

      if (!accepted)
      {
        LOG(INFO) << "An incoming instance has been discarded by the filter";
        return StoreStatus_FilteredOut;
      }

      if (compressionEnabled_)
      {
        accessor_.SetCompressionForNextOperations(CompressionType_Zlib);
      }
      else
      {
        accessor_.SetCompressionForNextOperations(CompressionType_None);
      }      

      FileInfo dicomInfo = accessor_.Write(dicom.GetBufferData(), dicom.GetBufferSize(), FileContentType_Dicom);
      FileInfo jsonInfo = accessor_.Write(dicom.GetJson().toStyledString(), FileContentType_DicomAsJson);

      ServerIndex::Attachments attachments;
      attachments.push_back(dicomInfo);
      attachments.push_back(jsonInfo);

      typedef std::map<MetadataType, std::string>  InstanceMetadata;
      InstanceMetadata  instanceMetadata;
      StoreStatus status = index_.Store(instanceMetadata, dicom.GetSummary(), attachments, 
                                        dicom.GetRemoteAet(), dicom.GetMetadata());

      // Only keep the metadata for the "instance" level
      dicom.GetMetadata().clear();

      for (InstanceMetadata::const_iterator it = instanceMetadata.begin();
           it != instanceMetadata.end(); ++it)
      {
        dicom.GetMetadata().insert(std::make_pair(std::make_pair(ResourceType_Instance, it->first),
                                                  it->second));
      }
            
      if (status != StoreStatus_Success)
      {
        accessor_.Remove(dicomInfo.GetUuid(), FileContentType_Dicom);
        accessor_.Remove(jsonInfo.GetUuid(), FileContentType_DicomAsJson);
      }

      switch (status)
      {
        case StoreStatus_Success:
          LOG(INFO) << "New instance stored";
          break;

        case StoreStatus_AlreadyStored:
          LOG(INFO) << "Already stored";
          break;

        case StoreStatus_Failure:
          LOG(ERROR) << "Store failure";
          break;

        default:
          // This should never happen
          break;
      }

      if (status == StoreStatus_Success ||
          status == StoreStatus_AlreadyStored)
      {
        boost::recursive_mutex::scoped_lock lock(listenersMutex_);

        for (ServerListeners::iterator it = listeners_.begin(); it != listeners_.end(); ++it)
        {
          try
          {
            it->GetListener().SignalStoredInstance(resultPublicId, dicom, simplified);
          }
          catch (OrthancException& e)
          {
            LOG(ERROR) << "Error in the " << it->GetDescription() 
                       << " callback while receiving an instance: " << e.What();
          }
        }
      }

      return status;
    }
    catch (OrthancException& e)
    {
      if (e.GetErrorCode() == ErrorCode_InexistentTag)
      {
        LogMissingRequiredTag(dicom.GetSummary());
      }

      throw;
    }
  }



  void ServerContext::AnswerAttachment(RestApiOutput& output,
                                       const std::string& instancePublicId,
                                       FileContentType content)
  {
    FileInfo attachment;
    if (!index_.LookupAttachment(attachment, instancePublicId, content))
    {
      throw OrthancException(ErrorCode_InternalError);
    }

    accessor_.SetCompressionForNextOperations(attachment.GetCompressionType());

    std::auto_ptr<HttpFileSender> sender(accessor_.ConstructHttpFileSender(attachment.GetUuid(), attachment.GetContentType()));
    sender->SetContentType(GetMimeType(content));
    sender->SetDownloadFilename(instancePublicId + ".dcm");
    output.AnswerFile(*sender);
  }


  void ServerContext::ReadJson(Json::Value& result,
                               const std::string& instancePublicId)
  {
    std::string s;
    ReadFile(s, instancePublicId, FileContentType_DicomAsJson);

    Json::Reader reader;
    if (!reader.parse(s, result))
    {
      throw OrthancException("Corrupted JSON file");
    }
  }


  void ServerContext::ReadFile(std::string& result,
                               const std::string& instancePublicId,
                               FileContentType content,
                               bool uncompressIfNeeded)
  {
    FileInfo attachment;
    if (!index_.LookupAttachment(attachment, instancePublicId, content))
    {
      throw OrthancException(ErrorCode_InternalError);
    }

    if (uncompressIfNeeded)
    {
      accessor_.SetCompressionForNextOperations(attachment.GetCompressionType());
    }
    else
    {
      accessor_.SetCompressionForNextOperations(CompressionType_None);
    }

    accessor_.Read(result, attachment.GetUuid(), attachment.GetContentType());
  }


  IDynamicObject* ServerContext::DicomCacheProvider::Provide(const std::string& instancePublicId)
  {
    std::string content;
    context_.ReadFile(content, instancePublicId, FileContentType_Dicom);
    return new ParsedDicomFile(content);
  }


  ServerContext::DicomCacheLocker::DicomCacheLocker(ServerContext& that,
                                                    const std::string& instancePublicId) : 
    that_(that),
    lock_(that_.dicomCacheMutex_)
  {
#if ENABLE_DICOM_CACHE == 0
    static std::auto_ptr<IDynamicObject> p;
    p.reset(provider_.Provide(instancePublicId));
    dicom_ = dynamic_cast<ParsedDicomFile*>(p.get());
#else
    dicom_ = &dynamic_cast<ParsedDicomFile&>(that_.dicomCache_.Access(instancePublicId));
#endif
  }


  ServerContext::DicomCacheLocker::~DicomCacheLocker()
  {
  }


  void ServerContext::SetStoreMD5ForAttachments(bool storeMD5)
  {
    LOG(INFO) << "Storing MD5 for attachments: " << (storeMD5 ? "yes" : "no");
    accessor_.SetStoreMD5(storeMD5);
  }


  bool ServerContext::AddAttachment(const std::string& resourceId,
                                    FileContentType attachmentType,
                                    const void* data,
                                    size_t size)
  {
    LOG(INFO) << "Adding attachment " << EnumerationToString(attachmentType) << " to resource " << resourceId;
    
    if (compressionEnabled_)
    {
      accessor_.SetCompressionForNextOperations(CompressionType_Zlib);
    }
    else
    {
      accessor_.SetCompressionForNextOperations(CompressionType_None);
    }      

    FileInfo info = accessor_.Write(data, size, attachmentType);
    StoreStatus status = index_.AddAttachment(info, resourceId);

    if (status != StoreStatus_Success)
    {
      accessor_.Remove(info.GetUuid(), info.GetContentType());
      return false;
    }
    else
    {
      return true;
    }
  }


  bool ServerContext::DeleteResource(Json::Value& target,
                                     const std::string& uuid,
                                     ResourceType expectedType)
  {
    return index_.DeleteResource(target, uuid, expectedType);
  }


  void ServerContext::SignalChange(const ServerIndexChange& change)
  {
    pendingChanges_.Enqueue(change.Clone());
  }


  void ServerContext::SetPlugins(OrthancPlugins& plugins)
  {
    boost::recursive_mutex::scoped_lock lock(listenersMutex_);

    plugins_ = &plugins;

    // TODO REFACTOR THIS
    listeners_.clear();
    listeners_.push_back(ServerListener(lua_, "Lua"));
    listeners_.push_back(ServerListener(plugins, "plugin"));
  }


  void ServerContext::ResetPlugins()
  {
    boost::recursive_mutex::scoped_lock lock(listenersMutex_);

    plugins_ = NULL;

    // TODO REFACTOR THIS
    listeners_.clear();
    listeners_.push_back(ServerListener(lua_, "Lua"));
  }


  bool ServerContext::HasPlugins() const
  {
    return (plugins_ != NULL);
  }


  const OrthancPlugins& ServerContext::GetPlugins() const
  {
    if (HasPlugins())
    {
      return *plugins_;
    }
    else
    {
      throw OrthancException(ErrorCode_InternalError);
    }
  }
}
