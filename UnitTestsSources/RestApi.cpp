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


#include "PrecompiledHeadersUnitTests.h"
#include "gtest/gtest.h"

#include <ctype.h>
#include <glog/logging.h>

#include "../Core/ChunkedBuffer.h"
#include "../Core/HttpClient.h"
#include "../Core/RestApi/RestApi.h"
#include "../Core/Uuid.h"
#include "../Core/OrthancException.h"
#include "../Core/Compression/ZlibCompressor.h"

using namespace Orthanc;

#if !defined(UNIT_TESTS_WITH_HTTP_CONNEXIONS)
#error "Please set UNIT_TESTS_WITH_HTTP_CONNEXIONS"
#endif

TEST(HttpClient, Basic)
{
  HttpClient c;
  ASSERT_FALSE(c.IsVerbose());
  c.SetVerbose(true);
  ASSERT_TRUE(c.IsVerbose());
  c.SetVerbose(false);
  ASSERT_FALSE(c.IsVerbose());

#if UNIT_TESTS_WITH_HTTP_CONNEXIONS == 1
  Json::Value v;
  c.SetUrl("http://orthanc.googlecode.com/hg/Resources/Configuration.json");
  c.Apply(v);
  ASSERT_TRUE(v.isMember("StorageDirectory"));
  //ASSERT_EQ(GetLastStatusText());

  v = Json::nullValue;

  HttpClient cc(c);
  cc.SetUrl("https://orthanc.googlecode.com/hg/Resources/Configuration.json");
  cc.Apply(v);
  ASSERT_TRUE(v.isMember("LuaScripts"));
#endif
}

TEST(RestApi, ChunkedBuffer)
{
  ChunkedBuffer b;
  ASSERT_EQ(0, b.GetNumBytes());

  b.AddChunk("hello", 5);
  ASSERT_EQ(5, b.GetNumBytes());

  b.AddChunk("world", 5);
  ASSERT_EQ(10, b.GetNumBytes());

  std::string s;
  b.Flatten(s);
  ASSERT_EQ("helloworld", s);
}

TEST(RestApi, ParseCookies)
{
  HttpHandler::Arguments headers;
  HttpHandler::Arguments cookies;

  headers["cookie"] = "a=b;c=d;;;e=f;;g=h;";
  HttpHandler::ParseCookies(cookies, headers);
  ASSERT_EQ(4u, cookies.size());
  ASSERT_EQ("b", cookies["a"]);
  ASSERT_EQ("d", cookies["c"]);
  ASSERT_EQ("f", cookies["e"]);
  ASSERT_EQ("h", cookies["g"]);

  headers["cookie"] = "  name =  value  ; name2=value2";
  HttpHandler::ParseCookies(cookies, headers);
  ASSERT_EQ(2u, cookies.size());
  ASSERT_EQ("value", cookies["name"]);
  ASSERT_EQ("value2", cookies["name2"]);

  headers["cookie"] = "  ;;;    ";
  HttpHandler::ParseCookies(cookies, headers);
  ASSERT_EQ(0u, cookies.size());

  headers["cookie"] = "  ;   n=v  ;;    ";
  HttpHandler::ParseCookies(cookies, headers);
  ASSERT_EQ(1u, cookies.size());
  ASSERT_EQ("v", cookies["n"]);
}

TEST(RestApi, RestApiPath)
{
  RestApiPath::Components args;
  UriComponents trail;

  {
    RestApiPath uri("/coucou/{abc}/d/*");
    ASSERT_TRUE(uri.Match(args, trail, "/coucou/moi/d/e/f/g"));
    ASSERT_EQ(1u, args.size());
    ASSERT_EQ(3u, trail.size());
    ASSERT_EQ("moi", args["abc"]);
    ASSERT_EQ("e", trail[0]);
    ASSERT_EQ("f", trail[1]);
    ASSERT_EQ("g", trail[2]);

    ASSERT_FALSE(uri.Match(args, trail, "/coucou/moi/f"));
    ASSERT_TRUE(uri.Match(args, trail, "/coucou/moi/d/"));
    ASSERT_FALSE(uri.Match(args, trail, "/a/moi/d"));
    ASSERT_FALSE(uri.Match(args, trail, "/coucou/moi"));
  }

  {
    RestApiPath uri("/coucou/{abc}/d");
    ASSERT_FALSE(uri.Match(args, trail, "/coucou/moi/d/e/f/g"));
    ASSERT_TRUE(uri.Match(args, trail, "/coucou/moi/d"));
    ASSERT_EQ(1u, args.size());
    ASSERT_EQ(0u, trail.size());
    ASSERT_EQ("moi", args["abc"]);
  }

  {
    RestApiPath uri("/*");
    ASSERT_TRUE(uri.Match(args, trail, "/a/b/c"));
    ASSERT_EQ(0u, args.size());
    ASSERT_EQ(3u, trail.size());
    ASSERT_EQ("a", trail[0]);
    ASSERT_EQ("b", trail[1]);
    ASSERT_EQ("c", trail[2]);
  }
}
