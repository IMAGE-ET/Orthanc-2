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


#include "../PrecompiledHeaders.h"
#include "LuaContext.h"

#include <set>
#include <glog/logging.h>
#include <cassert>
#include <boost/lexical_cast.hpp>

extern "C" 
{
#include <lualib.h>
#include <lauxlib.h>
}

namespace Orthanc
{
  LuaContext& LuaContext::GetLuaContext(lua_State *state)
  {
    const void* value = GetGlobalVariable(state, "_LuaContext");
    assert(value != NULL);

    return *const_cast<LuaContext*>(reinterpret_cast<const LuaContext*>(value));
  }

  int LuaContext::PrintToLog(lua_State *state)
  {
    LuaContext& that = GetLuaContext(state);

    // http://medek.wordpress.com/2009/02/03/wrapping-lua-errors-and-print-function/
    int nArgs = lua_gettop(state);
    lua_getglobal(state, "tostring");

    // Make sure you start at 1 *NOT* 0 for arrays in Lua.
    std::string result;

    for (int i = 1; i <= nArgs; i++)
    {
      const char *s;
      lua_pushvalue(state, -1);
      lua_pushvalue(state, i);
      lua_call(state, 1, 1);
      s = lua_tostring(state, -1);

      if (result.size() > 0)
        result.append(", ");

      if (s == NULL)
        result.append("<No conversion to string>");
      else
        result.append(s);
 
      lua_pop(state, 1);
    }

    LOG(WARNING) << "Lua says: " << result;         
    that.log_.append(result);
    that.log_.append("\n");

    return 0;
  }


  int LuaContext::ParseJson(lua_State *state)
  {
    LuaContext& that = GetLuaContext(state);

    int nArgs = lua_gettop(state);
    if (nArgs != 1 ||
        !lua_isstring(state, 1))    // Password
    {
      lua_pushnil(state);
      return 1;
    }

    const char* str = lua_tostring(state, 1);

    Json::Value value;
    Json::Reader reader;
    if (reader.parse(str, str + strlen(str), value))
    {
      that.PushJson(value);
    }
    else
    {
      lua_pushnil(state);
    }

    return 1;
  }


  int LuaContext::DumpJson(lua_State *state)
  {
    int nArgs = lua_gettop(state);
    if (nArgs != 1)
    {
      lua_pushnil(state);
      return 1;
    }

    Json::Value json;
    if (GetJson(json, state, 1))
    {
      Json::FastWriter writer;
      std::string s = writer.write(json);
      lua_pushstring(state, s.c_str());
    }
    else
    {
      LOG(ERROR) << "Lua: Unable to convert a JSON variable to a string";
      lua_pushnil(state);
    }

    return 1;
  }


  int LuaContext::SetHttpCredentials(lua_State *state)
  {
    LuaContext& that = GetLuaContext(state);

    // Check the types of the arguments
    int nArgs = lua_gettop(state);
    if (nArgs != 2 ||
        !lua_isstring(state, 1) ||  // Username
        !lua_isstring(state, 2))    // Password
    {
      LOG(ERROR) << "Lua: Bad parameters to SetHttpCredentials()";
    }
    else
    {
      // Configure the HTTP client
      const char* username = lua_tostring(state, 1);
      const char* password = lua_tostring(state, 2);
      that.httpClient_.SetCredentials(username, password);
    }

    return 0;
  }


  bool LuaContext::AnswerHttpQuery(lua_State* state)
  {
    std::string str;

    try
    {
      httpClient_.Apply(str);
    }
    catch (OrthancException& e)
    {
      return false;
    }

    // Return the result of the HTTP request
    lua_pushstring(state, str.c_str());

    return true;
  }

  
  int LuaContext::CallHttpGet(lua_State *state)
  {
    LuaContext& that = GetLuaContext(state);

    // Check the types of the arguments
    int nArgs = lua_gettop(state);
    if (nArgs != 1 || !lua_isstring(state, 1))  // URL
    {
      LOG(ERROR) << "Lua: Bad parameters to HttpGet()";
      lua_pushnil(state);
      return 1;
    }

    // Configure the HTTP client class
    const char* url = lua_tostring(state, 1);
    that.httpClient_.SetMethod(HttpMethod_Get);
    that.httpClient_.SetUrl(url);

    // Do the HTTP GET request
    if (!that.AnswerHttpQuery(state))
    {
      LOG(ERROR) << "Lua: Error in HttpGet() for URL " << url;
      lua_pushnil(state);
    }

    return 1;
  }


  int LuaContext::CallHttpPostOrPut(lua_State *state,
                                    HttpMethod method)
  {
    LuaContext& that = GetLuaContext(state);

    // Check the types of the arguments
    int nArgs = lua_gettop(state);
    if ((nArgs != 1 && nArgs != 2) ||
        !lua_isstring(state, 1) ||                // URL
        (nArgs >= 2 && !lua_isstring(state, 2)))  // Body data
    {
      LOG(ERROR) << "Lua: Bad parameters to HttpPost() or HttpPut()";
      lua_pushnil(state);
      return 1;
    }

    // Configure the HTTP client class
    const char* url = lua_tostring(state, 1);
    that.httpClient_.SetMethod(method);
    that.httpClient_.SetUrl(url);

    if (nArgs >= 2)
    {
      that.httpClient_.SetPostData(lua_tostring(state, 2));
    }
    else
    {
      that.httpClient_.AccessPostData().clear();
    }

    // Do the HTTP POST/PUT request
    if (!that.AnswerHttpQuery(state))
    {
      LOG(ERROR) << "Lua: Error in HttpPost() or HttpPut() for URL " << url;
      lua_pushnil(state);
    }

    return 1;
  }


  int LuaContext::CallHttpPost(lua_State *state)
  {
    return CallHttpPostOrPut(state, HttpMethod_Post);
  }


  int LuaContext::CallHttpPut(lua_State *state)
  {
    return CallHttpPostOrPut(state, HttpMethod_Put);
  }


  int LuaContext::CallHttpDelete(lua_State *state)
  {
    LuaContext& that = GetLuaContext(state);

    // Check the types of the arguments
    int nArgs = lua_gettop(state);
    if (nArgs != 1 || !lua_isstring(state, 1))  // URL
    {
      LOG(ERROR) << "Lua: Bad parameters to HttpDelete()";
      lua_pushnil(state);
      return 1;
    }

    // Configure the HTTP client class
    const char* url = lua_tostring(state, 1);
    that.httpClient_.SetMethod(HttpMethod_Delete);
    that.httpClient_.SetUrl(url);

    // Do the HTTP DELETE request
    std::string s;
    if (!that.httpClient_.Apply(s))
    {
      LOG(ERROR) << "Lua: Error in HttpDelete() for URL " << url;
      lua_pushnil(state);
    }
    else
    {
      lua_pushstring(state, "SUCCESS");
    }

    return 1;
  }


  void LuaContext::PushJson(const Json::Value& value)
  {
    if (value.isString())
    {
      lua_pushstring(lua_, value.asCString());
    }
    else if (value.isDouble())
    {
      lua_pushnumber(lua_, value.asDouble());
    }
    else if (value.isInt())
    {
      lua_pushinteger(lua_, value.asInt());
    }
    else if (value.isUInt())
    {
      lua_pushinteger(lua_, value.asUInt());
    }
    else if (value.isBool())
    {
      lua_pushboolean(lua_, value.asBool());
    }
    else if (value.isNull())
    {
      lua_pushnil(lua_);
    }
    else if (value.isArray())
    {
      lua_newtable(lua_);

      // http://lua-users.org/wiki/SimpleLuaApiExample
      for (Json::Value::ArrayIndex i = 0; i < value.size(); i++)
      {
        // Push the table index (note the "+1" because of Lua conventions)
        lua_pushnumber(lua_, i + 1);

        // Push the value of the cell
        PushJson(value[i]);

        // Stores the pair in the table
        lua_rawset(lua_, -3);
      }
    }
    else if (value.isObject())
    {
      lua_newtable(lua_);

      Json::Value::Members members = value.getMemberNames();

      for (Json::Value::Members::const_iterator 
             it = members.begin(); it != members.end(); ++it)
      {
        // Push the index of the cell
        lua_pushstring(lua_, it->c_str());

        // Push the value of the cell
        PushJson(value[*it]);

        // Stores the pair in the table
        lua_rawset(lua_, -3);
      }
    }
    else
    {
      throw LuaException("Unsupported JSON conversion");
    }
  }


  static bool CompactObjectToArray(Json::Value& result,
                                   const Json::Value& source)
  {
    Json::Value::Members members = source.getMemberNames();

    std::set<size_t> keys;
    for (Json::Value::ArrayIndex i = 0; i < members.size(); i++)
    {
      try
      {
        size_t key = boost::lexical_cast<size_t>(members[i]);
        keys.insert(key);
      }
      catch (boost::bad_lexical_cast&)
      {
        return false;
      }
    }

    if (keys.size() != members.size())
    {
      return false;
    }

    for (size_t i = 1; i <= members.size(); i++)
    {
      if (keys.find(i) == keys.end())
      {
        return false;
      }
    }

    result = Json::arrayValue;
    result.resize(members.size());
    for (size_t i = 0; i < members.size(); i++)
    {
      Json::Value::ArrayIndex key = boost::lexical_cast<Json::Value::ArrayIndex>(members[i]);
      assert(key > 0);
      result[key - 1] = source[members[i]];
    }

    return true;
  }


  bool LuaContext::GetJson(Json::Value& result,
                           lua_State *state,
                           int i)
  {
    // Caution: The order of the calls below is important, otherwise
    // Lua considers everything as a string.

    if (lua_isnil(state, i))
    {
      result = Json::nullValue;
      return true;
    }

    if (lua_isboolean(state, i))
    {
      result = lua_toboolean(state, i) ? true : false;
      return true;
    }

    if (lua_isnumber(state, i))
    {
      result = lua_tonumber(state, i);
      return true;
    }

    if (lua_isstring(state, i))
    {
      result = lua_tostring(state, i);
      return true;
    }

    if (lua_istable(state, i))
    {
      result = Json::objectValue;

      // http://stackoverflow.com/a/6142700/881731
      // Push another reference to the table on top of the stack (so we know
      // where it is, and this function can work for negative, positive and
      // pseudo indices
      lua_pushvalue(state, i);
      // stack now contains: -1 => table
      lua_pushnil(state);
      // stack now contains: -1 => nil; -2 => table
      while (lua_next(state, -2))
      {
        // stack now contains: -1 => value; -2 => key; -3 => table
        // copy the key so that lua_tostring does not modify the original
        lua_pushvalue(state, -2);
        // stack now contains: -1 => key; -2 => value; -3 => key; -4 => table
        const char *key = lua_tostring(state, -1);

        Json::Value item;
        if (!GetJson(item, state, -2))
        {
          lua_pop(state, 3);  // TODO IS THIS CORRECT?
          return false;
        }

        result[key] = item;

        // pop value + copy of key, leaving original key
        lua_pop(state, 2);
        // stack now contains: -1 => key; -2 => table
      }
      // stack now contains: -1 => table (when lua_next returns 0 it pops the key
      // but does not push anything.)
      // Pop table
      lua_pop(state, 1);

      // Stack is now the same as it was on entry to this function

      Json::Value array;
      if (CompactObjectToArray(array, result))
      {
        result = array;
      }

      return true;
    }

    return false;
  }


  LuaContext::LuaContext()
  {
    lua_ = luaL_newstate();
    if (!lua_)
    {
      throw LuaException("Unable to create the Lua context");
    }

    luaL_openlibs(lua_);
    lua_register(lua_, "print", PrintToLog);
    lua_register(lua_, "ParseJson", ParseJson);
    lua_register(lua_, "DumpJson", DumpJson);
    lua_register(lua_, "HttpGet", CallHttpGet);
    lua_register(lua_, "HttpPost", CallHttpPost);
    lua_register(lua_, "HttpPut", CallHttpPut);
    lua_register(lua_, "HttpDelete", CallHttpDelete);
    lua_register(lua_, "SetHttpCredentials", SetHttpCredentials);

    SetGlobalVariable("_LuaContext", this);
  }


  LuaContext::~LuaContext()
  {
    lua_close(lua_);
  }


  void LuaContext::ExecuteInternal(std::string* output,
                                   const std::string& command)
  {
    log_.clear();
    int error = (luaL_loadbuffer(lua_, command.c_str(), command.size(), "line") ||
                 lua_pcall(lua_, 0, 0, 0));

    if (error) 
    {
      assert(lua_gettop(lua_) >= 1);

      std::string description(lua_tostring(lua_, -1));
      lua_pop(lua_, 1); /* pop error message from the stack */
      LOG(ERROR) << "Error while executing Lua script: " << description;
      throw LuaException(description);
    }

    if (output != NULL)
    {
      *output = log_;
    }
  }


  void LuaContext::Execute(EmbeddedResources::FileResourceId resource)
  {
    std::string command;
    EmbeddedResources::GetFileResource(command, resource);
    ExecuteInternal(NULL, command);
  }


  bool LuaContext::IsExistingFunction(const char* name)
  {
    lua_settop(lua_, 0);
    lua_getglobal(lua_, name);
    return lua_type(lua_, -1) == LUA_TFUNCTION;
  }


  void LuaContext::Execute(Json::Value& output,
                           const std::string& command)
  {
    std::string s;
    ExecuteInternal(&s, command);

    Json::Reader reader;
    if (!reader.parse(s, output))
    {
      throw OrthancException(ErrorCode_BadFileFormat);
    }
  }


  void LuaContext::RegisterFunction(const char* name,
                                    lua_CFunction func)
  {
    lua_register(lua_, name, func);
  }


  void LuaContext::SetGlobalVariable(const char* name,
                                     void* value)
  {
    lua_pushlightuserdata(lua_, value);
    lua_setglobal(lua_, name);
  }

  
  const void* LuaContext::GetGlobalVariable(lua_State* state,
                                            const char* name)
  {
    lua_getglobal(state, name);
    assert(lua_type(state, -1) == LUA_TLIGHTUSERDATA);
    const void* value = lua_topointer(state, -1);
    lua_pop(state, 1);
    return value;
  }
}
