/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2014 Medical Physics Department, CHU of Liege,
 * Belgium
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


#pragma once

#include "IDatabaseWrapper.h"

#include "../Core/SQLite/Connection.h"
#include "../Core/SQLite/Transaction.h"

namespace Orthanc
{
  namespace Internals
  {
    class SignalRemainingAncestor;
  }

  /**
   * This class manages an instance of the Orthanc SQLite database. It
   * translates low-level requests into SQL statements. Mutual
   * exclusion MUST be implemented at a higher level.
   **/
  class DatabaseWrapper : public IDatabaseWrapper
  {
  private:
    IServerIndexListener* listener_;
    SQLite::Connection db_;
    Internals::SignalRemainingAncestor* signalRemainingAncestor_;

    void Open();

    void GetChangesInternal(std::list<ServerIndexChange>& target,
                            bool& done,
                            SQLite::Statement& s,
                            unsigned int maxResults);

    void GetExportedResourcesInternal(std::list<ExportedResource>& target,
                                      bool& done,
                                      SQLite::Statement& s,
                                      unsigned int maxResults);

  public:
    DatabaseWrapper(const std::string& path);

    DatabaseWrapper();

    virtual void SetListener(IServerIndexListener& listener);

    virtual void SetGlobalProperty(GlobalProperty property,
                                   const std::string& value);

    virtual bool LookupGlobalProperty(std::string& target,
                                      GlobalProperty property);

    virtual int64_t CreateResource(const std::string& publicId,
                                   ResourceType type);

    virtual bool LookupResource(const std::string& publicId,
                                int64_t& id,
                                ResourceType& type);

    virtual bool LookupParent(int64_t& parentId,
                              int64_t resourceId);

    virtual std::string GetPublicId(int64_t resourceId);

    virtual ResourceType GetResourceType(int64_t resourceId);

    virtual void AttachChild(int64_t parent,
                             int64_t child);

    virtual void DeleteResource(int64_t id);

    virtual void SetMetadata(int64_t id,
                             MetadataType type,
                             const std::string& value);

    virtual void DeleteMetadata(int64_t id,
                                MetadataType type);

    virtual bool LookupMetadata(std::string& target,
                                int64_t id,
                                MetadataType type);

    virtual void ListAvailableMetadata(std::list<MetadataType>& target,
                                       int64_t id);

    virtual void AddAttachment(int64_t id,
                               const FileInfo& attachment);

    virtual void DeleteAttachment(int64_t id,
                                  FileContentType attachment);

    virtual void ListAvailableAttachments(std::list<FileContentType>& result,
                                          int64_t id);

    virtual bool LookupAttachment(FileInfo& attachment,
                                  int64_t id,
                                  FileContentType contentType);

    virtual void SetMainDicomTags(int64_t id,
                                  const DicomMap& tags);

    virtual void GetMainDicomTags(DicomMap& map,
                                  int64_t id);

    virtual void GetChildrenPublicId(std::list<std::string>& result,
                                     int64_t id);

    virtual void GetChildrenInternalId(std::list<int64_t>& result,
                                       int64_t id);

    virtual void LogChange(int64_t internalId,
                           const ServerIndexChange& change);

    virtual void GetChanges(std::list<ServerIndexChange>& target /*out*/,
                            bool& done /*out*/,
                            int64_t since,
                            unsigned int maxResults);

    virtual void GetLastChange(std::list<ServerIndexChange>& target /*out*/);

    virtual void LogExportedResource(const ExportedResource& resource);
    
    virtual void GetExportedResources(std::list<ExportedResource>& target /*out*/,
                                      bool& done /*out*/,
                                      int64_t since,
                                      unsigned int maxResults);

    virtual void GetLastExportedResource(std::list<ExportedResource>& target /*out*/);

    virtual uint64_t GetTotalCompressedSize();
    
    virtual uint64_t GetTotalUncompressedSize();

    virtual uint64_t GetResourceCount(ResourceType resourceType);

    virtual void GetAllPublicIds(std::list<std::string>& target,
                                 ResourceType resourceType);

    virtual bool SelectPatientToRecycle(int64_t& internalId);

    virtual bool SelectPatientToRecycle(int64_t& internalId,
                                        int64_t patientIdToAvoid);

    virtual bool IsProtectedPatient(int64_t internalId);

    virtual void SetProtectedPatient(int64_t internalId, 
                                     bool isProtected);

    virtual SQLite::ITransaction* StartTransaction()
    {
      return new SQLite::Transaction(db_);
    }

    virtual void FlushToDisk()
    {
      db_.FlushToDisk();
    }

    virtual void ClearTable(const std::string& tableName);

    virtual bool IsExistingResource(int64_t internalId);

    virtual void LookupIdentifier(std::list<int64_t>& result,
                                  const DicomTag& tag,
                                  const std::string& value);

    virtual void LookupIdentifier(std::list<int64_t>& result,
                                  const std::string& value);

    virtual void GetAllMetadata(std::map<MetadataType, std::string>& result,
                                int64_t id);




    /**
     * The methods declared below are for unit testing only!
     **/

    const char* GetErrorMessage() const
    {
      return db_.GetErrorMessage();
    }

    void GetChildren(std::list<std::string>& childrenPublicIds,
                     int64_t id);

    int64_t GetTableRecordCount(const std::string& table);
    
    bool GetParentPublicId(std::string& result,
                           int64_t id);

  };
}
