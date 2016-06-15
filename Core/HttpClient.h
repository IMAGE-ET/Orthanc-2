/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
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


#pragma once

#include "Enumerations.h"
#include "WebServiceParameters.h"

#include <string>
#include <boost/shared_ptr.hpp>
#include <json/json.h>

namespace Orthanc
{
  class HttpClient
  {
  private:
    class GlobalParameters;

    struct PImpl;
    boost::shared_ptr<PImpl> pimpl_;

    std::string url_;
    std::string credentials_;
    HttpMethod method_;
    HttpStatus lastStatus_;
    std::string body_;  // This only makes sense for POST and PUT requests
    bool isVerbose_;
    long timeout_;
    std::string proxy_;
    bool verifyPeers_;
    std::string caCertificates_;
    std::string clientCertificateFile_;
    std::string clientCertificateKeyFile_;
    std::string clientCertificateKeyPassword_;

    void Setup();

    void operator= (const HttpClient&);  // Assignment forbidden
    HttpClient(const HttpClient& base);  // Copy forbidden

  public:
    HttpClient();

    HttpClient(const WebServiceParameters& service,
               const std::string& uri);

    ~HttpClient();

    void SetUrl(const char* url)
    {
      url_ = std::string(url);
    }

    void SetUrl(const std::string& url)
    {
      url_ = url;
    }

    const std::string& GetUrl() const
    {
      return url_;
    }

    void SetMethod(HttpMethod method)
    {
      method_ = method;
    }

    HttpMethod GetMethod() const
    {
      return method_;
    }

    void SetTimeout(long seconds)
    {
      timeout_ = seconds;
    }

    long GetTimeout() const
    {
      return timeout_;
    }

    void SetBody(const std::string& data)
    {
      body_ = data;
    }

    std::string& GetBody()
    {
      return body_;
    }

    const std::string& GetBody() const
    {
      return body_;
    }

    void SetVerbose(bool isVerbose);

    bool IsVerbose() const
    {
      return isVerbose_;
    }

    void AddHeader(const std::string& key,
                   const std::string& value);

    void ClearHeaders();

    bool Apply(std::string& answer);

    bool Apply(Json::Value& answer);

    HttpStatus GetLastStatus() const
    {
      return lastStatus_;
    }

    void SetCredentials(const char* username,
                        const char* password);

    void SetProxy(const std::string& proxy)
    {
      proxy_ = proxy;
    }

    void SetHttpsVerifyPeers(bool verify)
    {
      verifyPeers_ = verify;
    }

    bool IsHttpsVerifyPeers() const
    {
      return verifyPeers_;
    }

    void SetHttpsCACertificates(const std::string& certificates)
    {
      caCertificates_ = certificates;
    }

    const std::string& GetHttpsCACertificates() const
    {
      return caCertificates_;
    }

    void SetClientCertificate(const std::string& certificateFile,
                              const std::string& certificateKeyFile,
                              const std::string& certificateKeyPassword);

    const std::string& GetClientCertificateFile() const
    {
      return clientCertificateFile_;
    }

    const std::string& GetClientCertificateKeyFile() const
    {
      return clientCertificateKeyFile_;
    }

    const std::string& GetClientCertificateKeyPassword() const
    {
      return clientCertificateKeyPassword_;
    }

    static void GlobalInitialize();
  
    static void GlobalFinalize();

    static void ConfigureSsl(bool httpsVerifyPeers,
                             const std::string& httpsCACertificates);

    static void SetDefaultProxy(const std::string& proxy);

    static void SetDefaultTimeout(long timeout);

    void ApplyAndThrowException(std::string& answer);

    void ApplyAndThrowException(Json::Value& answer);
  };
}
