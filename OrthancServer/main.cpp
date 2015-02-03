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


#include "PrecompiledHeadersServer.h"
#include "OrthancRestApi/OrthancRestApi.h"

#include <fstream>
#include <glog/logging.h>
#include <boost/algorithm/string/predicate.hpp>

#include "../Core/Uuid.h"
#include "../Core/HttpServer/EmbeddedResourceHttpHandler.h"
#include "../Core/HttpServer/FilesystemHttpHandler.h"
#include "../Core/Lua/LuaFunctionCall.h"
#include "../Core/DicomFormat/DicomArray.h"
#include "DicomProtocol/DicomServer.h"
#include "DicomProtocol/ReusableDicomUserConnection.h"
#include "OrthancInitialization.h"
#include "ServerContext.h"
#include "OrthancFindRequestHandler.h"
#include "OrthancMoveRequestHandler.h"
#include "ServerToolbox.h"
#include "../Plugins/Engine/PluginsManager.h"
#include "../Plugins/Engine/OrthancPlugins.h"

using namespace Orthanc;


#define ENABLE_PLUGINS  1


class OrthancStoreRequestHandler : public IStoreRequestHandler
{
private:
  ServerContext& server_;

public:
  OrthancStoreRequestHandler(ServerContext& context) :
    server_(context)
  {
  }

  virtual void Handle(const std::string& dicomFile,
                      const DicomMap& dicomSummary,
                      const Json::Value& dicomJson,
                      const std::string& remoteAet)
  {
    if (dicomFile.size() > 0)
    {
      DicomInstanceToStore toStore;
      toStore.SetBuffer(dicomFile);
      toStore.SetSummary(dicomSummary);
      toStore.SetJson(dicomJson);
      toStore.SetRemoteAet(remoteAet);

      std::string id;
      server_.Store(id, toStore);
    }
  }
};



class MyDicomServerFactory : 
  public IStoreRequestHandlerFactory,
  public IFindRequestHandlerFactory, 
  public IMoveRequestHandlerFactory
{
private:
  ServerContext& context_;

public:
  MyDicomServerFactory(ServerContext& context) : context_(context)
  {
  }

  virtual IStoreRequestHandler* ConstructStoreRequestHandler()
  {
    return new OrthancStoreRequestHandler(context_);
  }

  virtual IFindRequestHandler* ConstructFindRequestHandler()
  {
    std::auto_ptr<OrthancFindRequestHandler> result(new OrthancFindRequestHandler(context_));

    result->SetMaxResults(Configuration::GetGlobalIntegerParameter("LimitFindResults", 0));
    result->SetMaxInstances(Configuration::GetGlobalIntegerParameter("LimitFindInstances", 0));

    if (result->GetMaxResults() == 0)
    {
      LOG(INFO) << "No limit on the number of C-FIND results at the Patient, Study and Series levels";
    }
    else
    {
      LOG(INFO) << "Maximum " << result->GetMaxResults() 
                << " results for C-FIND queries at the Patient, Study and Series levels";
    }

    if (result->GetMaxInstances() == 0)
    {
      LOG(INFO) << "No limit on the number of C-FIND results at the Instance level";
    }
    else
    {
      LOG(INFO) << "Maximum " << result->GetMaxInstances() 
                << " instances will be returned for C-FIND queries at the Instance level";
    }

    return result.release();
  }

  virtual IMoveRequestHandler* ConstructMoveRequestHandler()
  {
    return new OrthancMoveRequestHandler(context_);
  }

  void Done()
  {
  }
};


class OrthancApplicationEntityFilter : public IApplicationEntityFilter
{
private:
  ServerContext& context_;

public:
  OrthancApplicationEntityFilter(ServerContext& context) : context_(context)
  {
  }

  virtual bool IsAllowedConnection(const std::string& /*callingIp*/,
                                   const std::string& /*callingAet*/)
  {
    return true;
  }

  virtual bool IsAllowedRequest(const std::string& /*callingIp*/,
                                const std::string& callingAet,
                                DicomRequestType type)
  {
    if (type == DicomRequestType_Store)
    {
      // Incoming store requests are always accepted, even from unknown AET
      return true;
    }

    if (!Configuration::IsKnownAETitle(callingAet))
    {
      LOG(ERROR) << "Unknown remote DICOM modality AET: \"" << callingAet << "\"";
      return false;
    }
    else
    {
      return true;
    }
  }

  virtual bool IsAllowedTransferSyntax(const std::string& callingIp,
                                       const std::string& callingAet,
                                       TransferSyntax syntax)
  {
    std::string configuration;

    switch (syntax)
    {
      case TransferSyntax_Deflated:
        configuration = "DeflatedTransferSyntaxAccepted";
        break;

      case TransferSyntax_Jpeg:
        configuration = "JpegTransferSyntaxAccepted";
        break;

      case TransferSyntax_Jpeg2000:
        configuration = "Jpeg2000TransferSyntaxAccepted";
        break;

      case TransferSyntax_JpegLossless:
        configuration = "JpegLosslessTransferSyntaxAccepted";
        break;

      case TransferSyntax_Jpip:
        configuration = "JpipTransferSyntaxAccepted";
        break;

      case TransferSyntax_Mpeg2:
        configuration = "Mpeg2TransferSyntaxAccepted";
        break;

      case TransferSyntax_Rle:
        configuration = "RleTransferSyntaxAccepted";
        break;

      default: 
        throw OrthancException(ErrorCode_ParameterOutOfRange);
    }

    {
      std::string lua = "Is" + configuration;

      ServerContext::LuaContextLocker locker(context_);
      
      if (locker.GetLua().IsExistingFunction(lua.c_str()))
      {
        LuaFunctionCall call(locker.GetLua(), lua.c_str());
        call.PushString(callingAet);
        call.PushString(callingIp);
        return call.ExecutePredicate();
      }
    }

    return Configuration::GetGlobalBoolParameter(configuration, true);
  }
};


class MyIncomingHttpRequestFilter : public IIncomingHttpRequestFilter
{
private:
  ServerContext& context_;

public:
  MyIncomingHttpRequestFilter(ServerContext& context) : context_(context)
  {
  }

  virtual bool IsAllowed(HttpMethod method,
                         const char* uri,
                         const char* ip,
                         const char* username) const
  {
    static const char* HTTP_FILTER = "IncomingHttpRequestFilter";

    ServerContext::LuaContextLocker locker(context_);

    // Test if the instance must be filtered out
    if (locker.GetLua().IsExistingFunction(HTTP_FILTER))
    {
      LuaFunctionCall call(locker.GetLua(), HTTP_FILTER);

      switch (method)
      {
        case HttpMethod_Get:
          call.PushString("GET");
          break;

        case HttpMethod_Put:
          call.PushString("PUT");
          break;

        case HttpMethod_Post:
          call.PushString("POST");
          break;

        case HttpMethod_Delete:
          call.PushString("DELETE");
          break;

        default:
          return true;
      }

      call.PushString(uri);
      call.PushString(ip);
      call.PushString(username);

      if (!call.ExecutePredicate())
      {
        LOG(INFO) << "An incoming HTTP request has been discarded by the filter";
        return false;
      }
    }

    return true;
  }
};


static void PrintHelp(char* path)
{
  std::cout 
    << "Usage: " << path << " [OPTION]... [CONFIGURATION]" << std::endl
    << "Orthanc, lightweight, RESTful DICOM server for healthcare and medical research." << std::endl
    << std::endl
    << "If no configuration file is given on the command line, a set of default " << std::endl
    << "parameters is used. Please refer to the Orthanc homepage for the full " << std::endl
    << "instructions about how to use Orthanc " << std::endl
    << "<https://code.google.com/p/orthanc/wiki/OrthancCookbook>." << std::endl
    << std::endl
    << "Command-line options:" << std::endl
    << "  --help\t\tdisplay this help and exit" << std::endl
    << "  --logdir=[dir]\tdirectory where to store the log files" << std::endl
    << "\t\t\t(if not used, the logs are dumped to stderr)" << std::endl
    << "  --config=[file]\tcreate a sample configuration file and exit" << std::endl
    << "  --trace\t\thighest verbosity in logs (for debug)" << std::endl
    << "  --verbose\t\tbe verbose in logs" << std::endl
    << "  --version\t\toutput version information and exit" << std::endl
    << std::endl
    << "Exit status:" << std::endl
    << " 0  if OK," << std::endl
    << " -1  if error (have a look at the logs)." << std::endl
    << std::endl;
}


static void PrintVersion(char* path)
{
  std::cout
    << path << " " << ORTHANC_VERSION << std::endl
    << "Copyright (C) 2012-2014 Medical Physics Department, CHU of Liege (Belgium) " << std::endl
    << "Licensing GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>, with OpenSSL exception." << std::endl
    << "This is free software: you are free to change and redistribute it." << std::endl
    << "There is NO WARRANTY, to the extent permitted by law." << std::endl
    << std::endl
    << "Written by Sebastien Jodogne <s.jodogne@gmail.com>" << std::endl;
}



static void LoadLuaScripts(ServerContext& context)
{
  std::list<std::string> luaScripts;
  Configuration::GetGlobalListOfStringsParameter(luaScripts, "LuaScripts");
  for (std::list<std::string>::const_iterator
         it = luaScripts.begin(); it != luaScripts.end(); ++it)
  {
    std::string path = Configuration::InterpretStringParameterAsPath(*it);
    LOG(WARNING) << "Installing the Lua scripts from: " << path;
    std::string script;
    Toolbox::ReadFile(script, path);

    ServerContext::LuaContextLocker locker(context);
    locker.GetLua().Execute(script);
  }
}


static void LoadPlugins(PluginsManager& pluginsManager)
{
  std::list<std::string> plugins;
  Configuration::GetGlobalListOfStringsParameter(plugins, "Plugins");
  for (std::list<std::string>::const_iterator
         it = plugins.begin(); it != plugins.end(); ++it)
  {
    std::string path = Configuration::InterpretStringParameterAsPath(*it);
    LOG(WARNING) << "Registering a plugin from: " << path;
    pluginsManager.RegisterPlugin(path);
  }  
}



static bool StartOrthanc(int argc, char *argv[])
{
  std::auto_ptr<IDatabaseWrapper> database;
  database.reset(Configuration::CreateDatabaseWrapper());


  // "storage" must be declared BEFORE "ServerContext context", to
  // avoid mess in the invokation order of the destructors.
  std::auto_ptr<IStorageArea>  storage;

  ServerContext context(*database);

  context.SetCompressionEnabled(Configuration::GetGlobalBoolParameter("StorageCompression", false));
  context.SetStoreMD5ForAttachments(Configuration::GetGlobalBoolParameter("StoreMD5ForAttachments", true));

  LoadLuaScripts(context);

  try
  {
    context.GetIndex().SetMaximumPatientCount(Configuration::GetGlobalIntegerParameter("MaximumPatientCount", 0));
  }
  catch (...)
  {
    context.GetIndex().SetMaximumPatientCount(0);
  }

  try
  {
    uint64_t size = Configuration::GetGlobalIntegerParameter("MaximumStorageSize", 0);
    context.GetIndex().SetMaximumStorageSize(size * 1024 * 1024);
  }
  catch (...)
  {
    context.GetIndex().SetMaximumStorageSize(0);
  }

  MyDicomServerFactory serverFactory(context);
  bool isReset = false;
    
  {
    // DICOM server
    DicomServer dicomServer;
    OrthancApplicationEntityFilter dicomFilter(context);
    dicomServer.SetCalledApplicationEntityTitleCheck(Configuration::GetGlobalBoolParameter("DicomCheckCalledAet", false));
    dicomServer.SetStoreRequestHandlerFactory(serverFactory);
    dicomServer.SetMoveRequestHandlerFactory(serverFactory);
    dicomServer.SetFindRequestHandlerFactory(serverFactory);
    dicomServer.SetPortNumber(Configuration::GetGlobalIntegerParameter("DicomPort", 4242));
    dicomServer.SetApplicationEntityTitle(Configuration::GetGlobalStringParameter("DicomAet", "ORTHANC"));
    dicomServer.SetApplicationEntityFilter(dicomFilter);

    // HTTP server
    MyIncomingHttpRequestFilter httpFilter(context);
    MongooseServer httpServer;
    httpServer.SetPortNumber(Configuration::GetGlobalIntegerParameter("HttpPort", 8042));
    httpServer.SetRemoteAccessAllowed(Configuration::GetGlobalBoolParameter("RemoteAccessAllowed", false));
    httpServer.SetKeepAliveEnabled(Configuration::GetGlobalBoolParameter("KeepAlive", false));
    httpServer.SetIncomingHttpRequestFilter(httpFilter);

    httpServer.SetAuthenticationEnabled(Configuration::GetGlobalBoolParameter("AuthenticationEnabled", false));
    Configuration::SetupRegisteredUsers(httpServer);

    if (Configuration::GetGlobalBoolParameter("SslEnabled", false))
    {
      std::string certificate = Configuration::InterpretStringParameterAsPath(
        Configuration::GetGlobalStringParameter("SslCertificate", "certificate.pem"));
      httpServer.SetSslEnabled(true);
      httpServer.SetSslCertificate(certificate.c_str());
    }
    else
    {
      httpServer.SetSslEnabled(false);
    }

    OrthancRestApi restApi(context);

#if ORTHANC_STANDALONE == 1
    EmbeddedResourceHttpHandler staticResources("/app", EmbeddedResources::ORTHANC_EXPLORER);
#else
    FilesystemHttpHandler staticResources("/app", ORTHANC_PATH "/OrthancExplorer");
#endif

#if ENABLE_PLUGINS == 1
    OrthancPlugins orthancPlugins(context);
    orthancPlugins.SetCommandLineArguments(argc, argv);
    orthancPlugins.SetOrthancRestApi(restApi);

    PluginsManager pluginsManager;
    pluginsManager.RegisterServiceProvider(orthancPlugins);
    LoadPlugins(pluginsManager);
    httpServer.RegisterHandler(orthancPlugins);
    context.SetOrthancPlugins(pluginsManager, orthancPlugins);
#endif

    httpServer.RegisterHandler(staticResources);
    httpServer.RegisterHandler(restApi);


#if ENABLE_PLUGINS == 1
    // Prepare the storage area
    if (orthancPlugins.HasStorageArea())
    {
      LOG(WARNING) << "Using a custom storage area from plugins";
      storage.reset(orthancPlugins.GetStorageArea());
    }
    else
#endif
    {
      storage.reset(Configuration::CreateStorageArea());
    }
    
    context.SetStorageArea(*storage);


    // GO !!! Start the requested servers
    if (Configuration::GetGlobalBoolParameter("HttpServerEnabled", true))
    {
      httpServer.Start();
      LOG(WARNING) << "HTTP server listening on port: " << httpServer.GetPortNumber();
    }
    else
    {
      LOG(WARNING) << "The HTTP server is disabled";
    }

    if (Configuration::GetGlobalBoolParameter("DicomServerEnabled", true))
    {
      dicomServer.Start();
      LOG(WARNING) << "DICOM server listening on port: " << dicomServer.GetPortNumber();
    }
    else
    {
      LOG(WARNING) << "The DICOM server is disabled";
    }

    LOG(WARNING) << "Orthanc has started";
    Toolbox::ServerBarrier(restApi.ResetRequestReceivedFlag());
    isReset = restApi.ResetRequestReceivedFlag();

    if (isReset)
    {
      LOG(WARNING) << "Reset request received, restarting Orthanc";
    }

    // We're done
    LOG(WARNING) << "Orthanc is stopping";

#if ENABLE_PLUGINS == 1
    context.ResetOrthancPlugins();
    orthancPlugins.Stop();
    LOG(WARNING) << "    Plugins have stopped";
#endif

    dicomServer.Stop();
    LOG(WARNING) << "    DICOM server has stopped";

    httpServer.Stop();
    LOG(WARNING) << "    HTTP server has stopped";
  }

  serverFactory.Done();

  return isReset;
}




int main(int argc, char* argv[]) 
{
  // Initialize Google's logging library.
  FLAGS_logtostderr = true;
  FLAGS_minloglevel = 1;
  FLAGS_v = 0;

  for (int i = 1; i < argc; i++)
  {
    if (std::string(argv[i]) == "--help")
    {
      PrintHelp(argv[0]);
      return 0;
    }

    if (std::string(argv[i]) == "--version")
    {
      PrintVersion(argv[0]);
      return 0;
    }

    if (std::string(argv[i]) == "--verbose")
    {
      FLAGS_minloglevel = 0;
    }

    if (std::string(argv[i]) == "--trace")
    {
      FLAGS_minloglevel = 0;
      FLAGS_v = 1;
    }

    if (boost::starts_with(argv[i], "--logdir="))
    {
      FLAGS_logtostderr = false;
      FLAGS_log_dir = std::string(argv[i]).substr(9);
    }

    if (boost::starts_with(argv[i], "--config="))
    {
      std::string configurationSample;
      GetFileResource(configurationSample, EmbeddedResources::CONFIGURATION_SAMPLE);

#if defined(_WIN32)
      // Replace UNIX newlines with DOS newlines 
      boost::replace_all(configurationSample, "\n", "\r\n");
#endif

      std::string target = std::string(argv[i]).substr(9);
      std::ofstream f(target.c_str());
      f << configurationSample;
      f.close();
      return 0;
    }
  }

  google::InitGoogleLogging("Orthanc");

  const char* configurationFile = NULL;
  for (int i = 1; i < argc; i++)
  {
    // Use the first argument that does not start with a "-" as
    // the configuration file
    if (argv[i][0] != '-')
    {
      configurationFile = argv[i];
    }
  }

  LOG(WARNING) << "Orthanc version: " << ORTHANC_VERSION;

  int status = 0;
  try
  {
    for (;;)
    {
      OrthancInitialize(configurationFile);

      bool reset = StartOrthanc(argc, argv);
      if (reset)
      {
        OrthancFinalize();
      }
      else
      {
        break;
      }
    }
  }
  catch (const OrthancException& e)
  {
    LOG(ERROR) << "Uncaught exception, stopping now: [" << e.What() << "]";
    status = -1;
  }
  catch (const std::exception& e) 
  {
    LOG(ERROR) << "Uncaught exception, stopping now: [" << e.what() << "]";
    status = -1;
  }
  catch (const std::string& s) 
  {
    LOG(ERROR) << "Uncaught exception, stopping now: [" << s << "]";
    status = -1;
  }
  catch (...)
  {
    LOG(ERROR) << "Native exception, stopping now. Check your plugins, if any.";
    status = -1;
  }

  OrthancFinalize();

  LOG(WARNING) << "Orthanc has stopped";

  return status;
}
