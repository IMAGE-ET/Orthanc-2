/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017 Osimis, Belgium
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


#include "../PrecompiledHeadersServer.h"
#include "StorePeerCommand.h"

#include "../../Core/Logging.h"
#include "../../Core/HttpClient.h"

namespace Orthanc
{
  StorePeerCommand::StorePeerCommand(ServerContext& context,
                                     const WebServiceParameters& peer,
                                     bool ignoreExceptions) : 
    context_(context),
    peer_(peer),
    ignoreExceptions_(ignoreExceptions)
  {
  }

  bool StorePeerCommand::Apply(ListOfStrings& outputs,
                               const ListOfStrings& inputs)
  {
    // Configure the HTTP client
    HttpClient client(peer_, "instances");
    client.SetMethod(HttpMethod_Post);

    for (ListOfStrings::const_iterator
           it = inputs.begin(); it != inputs.end(); ++it)
    {
      LOG(INFO) << "Sending resource " << *it << " to peer \"" 
                << peer_.GetUrl() << "\"";

      try
      {
        context_.ReadDicom(client.GetBody(), *it);

        std::string answer;
        if (!client.Apply(answer))
        {
          LOG(ERROR) << "Unable to send resource " << *it << " to peer \"" << peer_.GetUrl() << "\"";
          throw OrthancException(ErrorCode_NetworkProtocol);
        }

        // Only chain with other commands if this command succeeds
        outputs.push_back(*it);
      }
      catch (OrthancException& e)
      {
        LOG(ERROR) << "Unable to forward to an Orthanc peer in a Lua script (instance " 
                   << *it << ", peer " << peer_.GetUrl() << "): " << e.What();

        if (!ignoreExceptions_)
        {
          throw;
        }
      }
    }

    return true;
  }
}
