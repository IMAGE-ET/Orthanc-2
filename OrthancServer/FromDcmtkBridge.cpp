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

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Internals/DicomImageDecoder.h"

#include "FromDcmtkBridge.h"
#include "ToDcmtkBridge.h"
#include "OrthancInitialization.h"
#include "../Core/Logging.h"
#include "../Core/Toolbox.h"
#include "../Core/OrthancException.h"
#include "../Core/Images/PngWriter.h"
#include "../Core/Uuid.h"
#include "../Core/DicomFormat/DicomString.h"
#include "../Core/DicomFormat/DicomNullValue.h"
#include "../Core/DicomFormat/DicomIntegerPixelAccessor.h"

#include <list>
#include <limits>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include <dcmtk/dcmdata/dcchrstr.h>
#include <dcmtk/dcmdata/dcdicent.h>
#include <dcmtk/dcmdata/dcdict.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcistrmb.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcmetinf.h>
#include <dcmtk/dcmdata/dcdeftag.h>

#include <dcmtk/dcmdata/dcvrae.h>
#include <dcmtk/dcmdata/dcvras.h>
#include <dcmtk/dcmdata/dcvrcs.h>
#include <dcmtk/dcmdata/dcvrda.h>
#include <dcmtk/dcmdata/dcvrds.h>
#include <dcmtk/dcmdata/dcvrdt.h>
#include <dcmtk/dcmdata/dcvrfd.h>
#include <dcmtk/dcmdata/dcvrfl.h>
#include <dcmtk/dcmdata/dcvris.h>
#include <dcmtk/dcmdata/dcvrlo.h>
#include <dcmtk/dcmdata/dcvrlt.h>
#include <dcmtk/dcmdata/dcvrpn.h>
#include <dcmtk/dcmdata/dcvrsh.h>
#include <dcmtk/dcmdata/dcvrsl.h>
#include <dcmtk/dcmdata/dcvrss.h>
#include <dcmtk/dcmdata/dcvrst.h>
#include <dcmtk/dcmdata/dcvrtm.h>
#include <dcmtk/dcmdata/dcvrui.h>
#include <dcmtk/dcmdata/dcvrul.h>
#include <dcmtk/dcmdata/dcvrus.h>
#include <dcmtk/dcmdata/dcvrut.h>
#include <dcmtk/dcmdata/dcpixel.h>
#include <dcmtk/dcmdata/dcpixseq.h>
#include <dcmtk/dcmdata/dcpxitem.h>
#include <dcmtk/dcmdata/dcvrat.h>

#include <dcmtk/dcmnet/dul.h>

#include <boost/math/special_functions/round.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <dcmtk/dcmdata/dcostrmb.h>


namespace Orthanc
{
  static inline uint16_t GetCharValue(char c)
  {
    if (c >= '0' && c <= '9')
      return c - '0';
    else if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    else
      return 0;
  }

  static inline uint16_t GetTagValue(const char* c)
  {
    return ((GetCharValue(c[0]) << 12) + 
            (GetCharValue(c[1]) << 8) + 
            (GetCharValue(c[2]) << 4) + 
            GetCharValue(c[3]));
  }


#if DCMTK_USE_EMBEDDED_DICTIONARIES == 1
  static void LoadEmbeddedDictionary(DcmDataDictionary& dictionary,
                                     EmbeddedResources::FileResourceId resource)
  {
    Toolbox::TemporaryFile tmp;

    FILE* fp = fopen(tmp.GetPath().c_str(), "wb");
    fwrite(EmbeddedResources::GetFileResourceBuffer(resource), 
           EmbeddedResources::GetFileResourceSize(resource), 1, fp);
    fclose(fp);

    if (!dictionary.loadDictionary(tmp.GetPath().c_str()))
    {
      throw OrthancException(ErrorCode_InternalError);
    }
  }
                             
#else
  static void LoadExternalDictionary(DcmDataDictionary& dictionary,
                                     const std::string& directory,
                                     const std::string& filename)
  {
    boost::filesystem::path p = directory;
    p = p / filename;

    LOG(WARNING) << "Loading the external DICOM dictionary " << p;

    if (!dictionary.loadDictionary(p.string().c_str()))
    {
      throw OrthancException(ErrorCode_InternalError);
    }
  }
                            
#endif


  namespace
  {
    class DictionaryLocker
    {
    private:
      DcmDataDictionary& dictionary_;

    public:
      DictionaryLocker() : dictionary_(dcmDataDict.wrlock())
      {
      }

      ~DictionaryLocker()
      {
        dcmDataDict.unlock();
      }

      DcmDataDictionary& operator*()
      {
        return dictionary_;
      }

      DcmDataDictionary* operator->()
      {
        return &dictionary_;
      }
    };
  }


  void FromDcmtkBridge::InitializeDictionary()
  {
    /* Disable "gethostbyaddr" (which results in memory leaks) and use raw IP addresses */
    dcmDisableGethostbyaddr.set(OFTrue);

    {
      DictionaryLocker locker;

      locker->clear();

#if DCMTK_USE_EMBEDDED_DICTIONARIES == 1
      LOG(WARNING) << "Loading the embedded dictionaries";
      /**
       * Do not load DICONDE dictionary, it breaks the other tags. The
       * command "strace storescu 2>&1 |grep dic" shows that DICONDE
       * dictionary is not loaded by storescu.
       **/
      //LoadEmbeddedDictionary(*locker, EmbeddedResources::DICTIONARY_DICONDE);

      LoadEmbeddedDictionary(*locker, EmbeddedResources::DICTIONARY_DICOM);
      LoadEmbeddedDictionary(*locker, EmbeddedResources::DICTIONARY_PRIVATE);

#elif defined(__linux) || defined(__FreeBSD_kernel__)
      std::string path = DCMTK_DICTIONARY_DIR;

      const char* env = std::getenv(DCM_DICT_ENVIRONMENT_VARIABLE);
      if (env != NULL)
      {
        path = std::string(env);
      }

      LoadExternalDictionary(*locker, path, "dicom.dic");
      LoadExternalDictionary(*locker, path, "private.dic");

#else
#error Support your platform here
#endif
    }

    /* make sure data dictionary is loaded */
    if (!dcmDataDict.isDictionaryLoaded())
    {
      LOG(ERROR) << "No DICOM dictionary loaded, check environment variable: " << DCM_DICT_ENVIRONMENT_VARIABLE;
      throw OrthancException(ErrorCode_InternalError);
    }

    {
      // Test the dictionary with a simple DICOM tag
      DcmTag key(0x0010, 0x1030); // This is PatientWeight
      if (key.getEVR() != EVR_DS)
      {
        LOG(ERROR) << "The DICOM dictionary has not been correctly read";
        throw OrthancException(ErrorCode_InternalError);
      }
    }
  }


  void FromDcmtkBridge::RegisterDictionaryTag(const DicomTag& tag,
                                              const DcmEVR& vr,
                                              const std::string& name,
                                              unsigned int minMultiplicity,
                                              unsigned int maxMultiplicity)
  {
    if (minMultiplicity < 1)
    {
      throw OrthancException(ErrorCode_ParameterOutOfRange);
    }

    if (maxMultiplicity == 0)
    {
      maxMultiplicity = DcmVariableVM;
    }
    else if (maxMultiplicity < minMultiplicity)
    {
      throw OrthancException(ErrorCode_ParameterOutOfRange);
    }
    
    std::auto_ptr<DcmDictEntry>  entry(new DcmDictEntry(tag.GetGroup(),
                                                        tag.GetElement(),
                                                        vr, name.c_str(),
                                                        static_cast<int>(minMultiplicity),
                                                        static_cast<int>(maxMultiplicity),
                                                        NULL    /* version */,
                                                        OFTrue  /* doCopyString */,
                                                        NULL    /* private creator */));

    entry->setGroupRangeRestriction(DcmDictRange_Unspecified);
    entry->setElementRangeRestriction(DcmDictRange_Unspecified);

    {
      DictionaryLocker locker;
      locker->addEntry(entry.release());
    }
  }


  Encoding FromDcmtkBridge::DetectEncoding(DcmDataset& dataset)
  {
    // By default, Latin1 encoding is assumed
    std::string s = Configuration::GetGlobalStringParameter("DefaultEncoding", "Latin1");
    Encoding encoding = s.empty() ? Encoding_Latin1 : StringToEncoding(s.c_str());

    OFString tmp;
    if (dataset.findAndGetOFString(DCM_SpecificCharacterSet, tmp).good())
    {
      std::string characterSet = Toolbox::StripSpaces(std::string(tmp.c_str()));

      if (characterSet.empty())
      {
        // Empty specific character set tag: Use the default encoding
      }
      else if (GetDicomEncoding(encoding, characterSet.c_str()))
      {
        // The specific character set is supported by the Orthanc core
      }
      else
      {
        LOG(WARNING) << "Value of Specific Character Set (0008,0005) is not supported: " << characterSet
                     << ", fallback to ASCII (remove all special characters)";
        encoding = Encoding_Ascii;
      }
    }
    else
    {
      // No specific character set tag: Use the default encoding
    }

    return encoding;
  }


  void FromDcmtkBridge::Convert(DicomMap& target, DcmDataset& dataset)
  {
    Encoding encoding = DetectEncoding(dataset);

    target.Clear();
    for (unsigned long i = 0; i < dataset.card(); i++)
    {
      DcmElement* element = dataset.getElement(i);
      if (element && element->isLeaf())
      {
        target.SetValue(element->getTag().getGTag(),
                        element->getTag().getETag(),
                        ConvertLeafElement(*element, encoding));
      }
    }
  }


  DicomTag FromDcmtkBridge::Convert(const DcmTag& tag)
  {
    return DicomTag(tag.getGTag(), tag.getETag());
  }


  DicomTag FromDcmtkBridge::GetTag(const DcmElement& element)
  {
    return DicomTag(element.getGTag(), element.getETag());
  }


  bool FromDcmtkBridge::IsPrivateTag(DcmTag& tag)
  {
#if 1
    DcmTagKey tmp(tag.getGTag(), tag.getETag());
    return tmp.isPrivate();
#else
    // Implementation for Orthanc versions <= 0.8.5
    return (tag.getPrivateCreator() != NULL ||
            !strcmp("PrivateCreator", tag.getTagName()));  // TODO - This may change with future versions of DCMTK
#endif
  }


  bool FromDcmtkBridge::IsPrivateTag(const DicomTag& tag)
  {
#if 1
    DcmTagKey tmp(tag.GetGroup(), tag.GetElement());
    return tmp.isPrivate();
#else
    // Implementation for Orthanc versions <= 0.8.5
    DcmTag tmp(tag.GetGroup(), tag.GetElement());
    return IsPrivateTag(tmp);
#endif
  }


  DicomValue* FromDcmtkBridge::ConvertLeafElement(DcmElement& element,
                                                  Encoding encoding)
  {
    if (!element.isLeaf())
    {
      // This function is only applicable to leaf elements
      throw OrthancException(ErrorCode_BadParameterType);
    }

    if (element.isaString())
    {
      char *c;
      if (element.getString(c).good())
      {
        if (c == NULL)  // This case corresponds to the empty string
        {
          return new DicomString("");
        }
        else
        {
          std::string s(c);
          std::string utf8 = Toolbox::ConvertToUtf8(s, encoding);
          return new DicomString(utf8);
        }
      }
      else
      {
        return new DicomNullValue;
      }
    }

    try
    {
      // http://support.dcmtk.org/docs/dcvr_8h-source.html
      switch (element.getVR())
      {

        /**
         * TODO.
         **/

        case EVR_OB:  // other byte
        case EVR_OF:  // other float
        case EVR_OW:  // other word
        case EVR_UN:  // unknown value representation
          return new DicomNullValue;
    
          /**
           * String types, should never happen at this point because of
           * "element.isaString()".
           **/
      
        case EVR_DS:  // decimal string
        case EVR_IS:  // integer string
        case EVR_AS:  // age string
        case EVR_DA:  // date string
        case EVR_DT:  // date time string
        case EVR_TM:  // time string
        case EVR_AE:  // application entity title
        case EVR_CS:  // code string
        case EVR_SH:  // short string
        case EVR_LO:  // long string
        case EVR_ST:  // short text
        case EVR_LT:  // long text
        case EVR_UT:  // unlimited text
        case EVR_PN:  // person name
        case EVR_UI:  // unique identifier
          return new DicomNullValue;


          /**
           * Numberic types
           **/ 
      
        case EVR_SL:  // signed long
        {
          Sint32 f;
          if (dynamic_cast<DcmSignedLong&>(element).getSint32(f).good())
            return new DicomString(boost::lexical_cast<std::string>(f));
          else
            return new DicomNullValue;
        }

        case EVR_SS:  // signed short
        {
          Sint16 f;
          if (dynamic_cast<DcmSignedShort&>(element).getSint16(f).good())
            return new DicomString(boost::lexical_cast<std::string>(f));
          else
            return new DicomNullValue;
        }

        case EVR_UL:  // unsigned long
        {
          Uint32 f;
          if (dynamic_cast<DcmUnsignedLong&>(element).getUint32(f).good())
            return new DicomString(boost::lexical_cast<std::string>(f));
          else
            return new DicomNullValue;
        }

        case EVR_US:  // unsigned short
        {
          Uint16 f;
          if (dynamic_cast<DcmUnsignedShort&>(element).getUint16(f).good())
            return new DicomString(boost::lexical_cast<std::string>(f));
          else
            return new DicomNullValue;
        }

        case EVR_FL:  // float single-precision
        {
          Float32 f;
          if (dynamic_cast<DcmFloatingPointSingle&>(element).getFloat32(f).good())
            return new DicomString(boost::lexical_cast<std::string>(f));
          else
            return new DicomNullValue;
        }

        case EVR_FD:  // float double-precision
        {
          Float64 f;
          if (dynamic_cast<DcmFloatingPointDouble&>(element).getFloat64(f).good())
            return new DicomString(boost::lexical_cast<std::string>(f));
          else
            return new DicomNullValue;
        }


        /**
         * Attribute tag.
         **/

        case EVR_AT:
        {
          DcmTagKey tag;
          if (dynamic_cast<DcmAttributeTag&>(element).getTagVal(tag, 0).good())
          {
            DicomTag t(tag.getGroup(), tag.getElement());
            return new DicomString(t.Format());
          }
          else
          {
            return new DicomNullValue;
          }
        }


        /**
         * Sequence types, should never occur at this point because of
         * "element.isLeaf()".
         **/

        case EVR_SQ:  // sequence of items
          return new DicomNullValue;


          /**
           * Internal to DCMTK.
           **/ 

        case EVR_ox:  // OB or OW depending on context
        case EVR_xs:  // SS or US depending on context
        case EVR_lt:  // US, SS or OW depending on context, used for LUT Data (thus the name)
        case EVR_na:  // na="not applicable", for data which has no VR
        case EVR_up:  // up="unsigned pointer", used internally for DICOMDIR suppor
        case EVR_item:  // used internally for items
        case EVR_metainfo:  // used internally for meta info datasets
        case EVR_dataset:  // used internally for datasets
        case EVR_fileFormat:  // used internally for DICOM files
        case EVR_dicomDir:  // used internally for DICOMDIR objects
        case EVR_dirRecord:  // used internally for DICOMDIR records
        case EVR_pixelSQ:  // used internally for pixel sequences in a compressed image
        case EVR_pixelItem:  // used internally for pixel items in a compressed image
        case EVR_UNKNOWN: // used internally for elements with unknown VR (encoded with 4-byte length field in explicit VR)
        case EVR_PixelData:  // used internally for uncompressed pixeld data
        case EVR_OverlayData:  // used internally for overlay data
        case EVR_UNKNOWN2B:  // used internally for elements with unknown VR with 2-byte length field in explicit VR
          return new DicomNullValue;


          /**
           * Default case.
           **/ 

        default:
          return new DicomNullValue;
      }
    }
    catch (boost::bad_lexical_cast)
    {
      return new DicomNullValue;
    }
    catch (std::bad_cast)
    {
      return new DicomNullValue;
    }
  }


  static Json::Value& PrepareNode(Json::Value& parent,
                                  DcmElement& element,
                                  DicomToJsonFormat format)
  {
    assert(parent.type() == Json::objectValue);

    DicomTag tag(FromDcmtkBridge::GetTag(element));
    const std::string formattedTag = tag.Format();

    if (format == DicomToJsonFormat_Short)
    {
      parent[formattedTag] = Json::nullValue;
      return parent[formattedTag];
    }

    // This code gives access to the name of the private tags
    DcmTag tagbis(element.getTag());
    const std::string tagName(tagbis.getTagName());      
    
    switch (format)
    {
      case DicomToJsonFormat_Simple:
        parent[tagName] = Json::nullValue;
        return parent[tagName];

      case DicomToJsonFormat_Full:
      {
        parent[formattedTag] = Json::objectValue;
        Json::Value& node = parent[formattedTag];

        if (element.isLeaf())
        {
          node["Name"] = tagName;

          if (tagbis.getPrivateCreator() != NULL)
          {
            node["PrivateCreator"] = tagbis.getPrivateCreator();
          }

          return node;
        }
        else
        {
          node["Name"] = tagName;
          node["Type"] = "Sequence";
          node["Value"] = Json::nullValue;
          return node["Value"];
        }
      }

      default:
        throw OrthancException(ErrorCode_ParameterOutOfRange);
    }
  }


  static void LeafValueToJson(Json::Value& target,
                              const DicomValue& value,
                              DicomToJsonFormat format,
                              unsigned int maxStringLength)
  {
    std::string content = value.AsString();

    switch (format)
    {
      case DicomToJsonFormat_Short:
      case DicomToJsonFormat_Simple:
      {
        assert(target.type() == Json::nullValue);

        if (!value.IsNull() &&
            (maxStringLength == 0 ||
             content.size() <= maxStringLength))
        {
          target = content;
        }

        break;
      }      

      case DicomToJsonFormat_Full:
      {
        assert(target.type() == Json::objectValue);

        if (value.IsNull())
        {
          target["Type"] = "Null";
          target["Value"] = Json::nullValue;
        }
        else
        {
          if (maxStringLength == 0 ||
              content.size() <= maxStringLength)
          {
            target["Type"] = "String";
            target["Value"] = content;
          }
          else
          {
            target["Type"] = "TooLong";
            target["Value"] = Json::nullValue;
          }
        }
        break;
      }

      default:
        throw OrthancException(ErrorCode_ParameterOutOfRange);
    }
  }                              


  static void DatasetToJson(Json::Value& parent,
                            DcmItem& item,
                            DicomToJsonFormat format,
                            unsigned int maxStringLength,
                            Encoding encoding);


  void FromDcmtkBridge::ToJson(Json::Value& parent,
                               DcmElement& element,
                               DicomToJsonFormat format,
                               unsigned int maxStringLength,
                               Encoding encoding)
  {
    if (parent.type() == Json::nullValue)
    {
      parent = Json::objectValue;
    }

    assert(parent.type() == Json::objectValue);
    Json::Value& target = PrepareNode(parent, element, format);

    if (element.isLeaf())
    {
      std::auto_ptr<DicomValue> v(FromDcmtkBridge::ConvertLeafElement(element, encoding));
      LeafValueToJson(target, *v, format, maxStringLength);
    }
    else
    {
      assert(target.type() == Json::nullValue);
      target = Json::arrayValue;

      // "All subclasses of DcmElement except for DcmSequenceOfItems
      // are leaf nodes, while DcmSequenceOfItems, DcmItem, DcmDataset
      // etc. are not." The following dynamic_cast is thus OK.
      DcmSequenceOfItems& sequence = dynamic_cast<DcmSequenceOfItems&>(element);

      for (unsigned long i = 0; i < sequence.card(); i++)
      {
        DcmItem* child = sequence.getItem(i);
        Json::Value& v = target.append(Json::objectValue);
        DatasetToJson(v, *child, format, maxStringLength, encoding);
      }
    }
  }


  static void DatasetToJson(Json::Value& parent,
                            DcmItem& item,
                            DicomToJsonFormat format,
                            unsigned int maxStringLength,
                            Encoding encoding)
  {
    assert(parent.type() == Json::objectValue);

    for (unsigned long i = 0; i < item.card(); i++)
    {
      DcmElement* element = item.getElement(i);
      FromDcmtkBridge::ToJson(parent, *element, format, maxStringLength, encoding);
    }
  }


  void FromDcmtkBridge::ToJson(Json::Value& target, 
                               DcmDataset& dataset,
                               DicomToJsonFormat format,
                               unsigned int maxStringLength)
  {
    target = Json::objectValue;
    DatasetToJson(target, dataset, format, maxStringLength, DetectEncoding(dataset));
  }


  std::string FromDcmtkBridge::GetName(const DicomTag& t)
  {
    // Some patches for important tags because of different DICOM
    // dictionaries between DCMTK versions
    std::string n = t.GetMainTagsName();
    if (n.size() != 0)
    {
      return n;
    }
    // End of patches

#if 0
    DcmTagKey tag(t.GetGroup(), t.GetElement());
    const DcmDataDictionary& dict = dcmDataDict.rdlock();
    const DcmDictEntry* entry = dict.findEntry(tag, NULL);

    std::string s(DcmTag_ERROR_TagName);
    if (entry != NULL)
    {
      s = std::string(entry->getTagName());
    }

    dcmDataDict.unlock();
    return s;
#else
    DcmTag tag(t.GetGroup(), t.GetElement());
    const char* name = tag.getTagName();
    if (name == NULL)
    {
      return DcmTag_ERROR_TagName;
    }
    else
    {
      return std::string(name);
    }
#endif
  }


  DicomTag FromDcmtkBridge::ParseTag(const char* name)
  {
    if (strlen(name) == 9 &&
        isxdigit(name[0]) &&
        isxdigit(name[1]) &&
        isxdigit(name[2]) &&
        isxdigit(name[3]) &&
        (name[4] == '-' || name[4] == ',') &&
        isxdigit(name[5]) &&
        isxdigit(name[6]) &&
        isxdigit(name[7]) &&
        isxdigit(name[8]))        
    {
      uint16_t group = GetTagValue(name);
      uint16_t element = GetTagValue(name + 5);
      return DicomTag(group, element);
    }

#if 0
    const DcmDataDictionary& dict = dcmDataDict.rdlock();
    const DcmDictEntry* entry = dict.findEntry(name);

    if (entry == NULL)
    {
      dcmDataDict.unlock();
      throw OrthancException(ErrorCode_UnknownDicomTag);
    }
    else
    {
      DcmTagKey key = entry->getKey();
      DicomTag tag(key.getGroup(), key.getElement());
      dcmDataDict.unlock();
      return tag;
    }
#else
    DcmTag tag;
    if (DcmTag::findTagFromName(name, tag).good())
    {
      return DicomTag(tag.getGTag(), tag.getETag());
    }
    else
    {
      throw OrthancException(ErrorCode_UnknownDicomTag);
    }
#endif
  }


  bool FromDcmtkBridge::IsUnknownTag(const DicomTag& tag)
  {
    DcmTag tmp(tag.GetGroup(), tag.GetElement());
    return tmp.isUnknownVR();
  }


  void FromDcmtkBridge::Print(FILE* fp, const DicomMap& m)
  {
    for (DicomMap::Map::const_iterator 
           it = m.map_.begin(); it != m.map_.end(); ++it)
    {
      DicomTag t = it->first;
      std::string s = it->second->AsString();
      fprintf(fp, "0x%04x 0x%04x (%s) [%s]\n", t.GetGroup(), t.GetElement(), GetName(t).c_str(), s.c_str());
    }
  }


  void FromDcmtkBridge::ToJson(Json::Value& result,
                               const DicomMap& values,
                               bool simplify)
  {
    if (result.type() != Json::objectValue)
    {
      throw OrthancException(ErrorCode_BadParameterType);
    }

    result.clear();

    for (DicomMap::Map::const_iterator 
           it = values.map_.begin(); it != values.map_.end(); ++it)
    {
      if (simplify)
      {
        result[GetName(it->first)] = it->second->AsString();
      }
      else
      {
        Json::Value value = Json::objectValue;

        value["Name"] = GetName(it->first);

        if (it->second->IsNull())
        {
          value["Type"] = "Null";
          value["Value"] = Json::nullValue;
        }
        else
        {
          value["Type"] = "String";
          value["Value"] = it->second->AsString();
        }

        result[it->first.Format()] = value;
      }
    }
  }


  std::string FromDcmtkBridge::GenerateUniqueIdentifier(ResourceType level)
  {
    char uid[100];

    switch (level)
    {
      case ResourceType_Patient:
        // The "PatientID" field is of type LO (Long String), 64
        // Bytes Maximum. An UUID is of length 36, thus it can be used
        // as a random PatientID.
        return Toolbox::GenerateUuid();

      case ResourceType_Instance:
        return dcmGenerateUniqueIdentifier(uid, SITE_INSTANCE_UID_ROOT);

      case ResourceType_Series:
        return dcmGenerateUniqueIdentifier(uid, SITE_SERIES_UID_ROOT);

      case ResourceType_Study:
        return dcmGenerateUniqueIdentifier(uid, SITE_STUDY_UID_ROOT);

      default:
        throw OrthancException(ErrorCode_ParameterOutOfRange);
    }
  }

  bool FromDcmtkBridge::SaveToMemoryBuffer(std::string& buffer,
                                           DcmDataset& dataSet)
  {
    // Determine the transfer syntax which shall be used to write the
    // information to the file. We always switch to the Little Endian
    // syntax, with explicit length.

    // http://support.dcmtk.org/docs/dcxfer_8h-source.html


    /**
     * Note that up to Orthanc 0.7.1 (inclusive), the
     * "EXS_LittleEndianExplicit" was always used to save the DICOM
     * dataset into memory. We now keep the original transfer syntax
     * (if available).
     **/
    E_TransferSyntax xfer = dataSet.getOriginalXfer();
    if (xfer == EXS_Unknown)
    {
      // No information about the original transfer syntax: This is
      // most probably a DICOM dataset that was read from memory.
      xfer = EXS_LittleEndianExplicit;
    }

    E_EncodingType encodingType = /*opt_sequenceType*/ EET_ExplicitLength;

    // Create the meta-header information
    DcmFileFormat ff(&dataSet);
    ff.validateMetaInfo(xfer);
    ff.removeInvalidGroups();

    // Create a memory buffer with the proper size
    uint32_t s = ff.calcElementLength(xfer, encodingType);
    buffer.resize(s);
    DcmOutputBufferStream ob(&buffer[0], s);

    // Fill the memory buffer with the meta-header and the dataset
    ff.transferInit();
    OFCondition c = ff.write(ob, xfer, encodingType, NULL,
                             /*opt_groupLength*/ EGL_recalcGL,
                             /*opt_paddingType*/ EPD_withoutPadding);
    ff.transferEnd();

    // Handle errors
    if (c.good())
    {
      return true;
    }
    else
    {
      buffer.clear();
      return false;
    }
  }


  ValueRepresentation FromDcmtkBridge::GetValueRepresentation(const DicomTag& tag)
  {
    DcmTag t(tag.GetGroup(), tag.GetElement());
    switch (t.getEVR())
    {
      case EVR_PN:
        return ValueRepresentation_PatientName;

      case EVR_DA:
        return ValueRepresentation_Date;

      case EVR_DT:
        return ValueRepresentation_DateTime;

      case EVR_TM:
        return ValueRepresentation_Time;

      default:
        return ValueRepresentation_Other;
    }
  }


  static bool IsBinaryTag(const DcmTag& key)
  {
    return key.isPrivate() || key.isUnknownVR();
  }


  DcmElement* FromDcmtkBridge::CreateElementForTag(const DicomTag& tag)
  {
    DcmTag key(tag.GetGroup(), tag.GetElement());

    if (IsBinaryTag(key))
    {
      return new DcmOtherByteOtherWord(key);
    }

    switch (key.getEVR())
    {
      // http://support.dcmtk.org/docs/dcvr_8h-source.html

      /**
       * TODO.
       **/
    
      case EVR_OB:  // other byte
      case EVR_OF:  // other float
      case EVR_OW:  // other word
      case EVR_AT:  // attribute tag
        throw OrthancException(ErrorCode_NotImplemented);

      case EVR_UN:  // unknown value representation
        throw OrthancException(ErrorCode_ParameterOutOfRange);


      /**
       * String types.
       * http://support.dcmtk.org/docs/classDcmByteString.html
       **/
      
      case EVR_AS:  // age string
        return new DcmAgeString(key);

      case EVR_AE:  // application entity title
        return new DcmApplicationEntity(key);

      case EVR_CS:  // code string
        return new DcmCodeString(key);        

      case EVR_DA:  // date string
        return new DcmDate(key);
        
      case EVR_DT:  // date time string
        return new DcmDateTime(key);

      case EVR_DS:  // decimal string
        return new DcmDecimalString(key);

      case EVR_IS:  // integer string
        return new DcmIntegerString(key);

      case EVR_TM:  // time string
        return new DcmTime(key);

      case EVR_UI:  // unique identifier
        return new DcmUniqueIdentifier(key);

      case EVR_ST:  // short text
        return new DcmShortText(key);

      case EVR_LO:  // long string
        return new DcmLongString(key);

      case EVR_LT:  // long text
        return new DcmLongText(key);

      case EVR_UT:  // unlimited text
        return new DcmUnlimitedText(key);

      case EVR_SH:  // short string
        return new DcmShortString(key);

      case EVR_PN:  // person name
        return new DcmPersonName(key);

        
      /**
       * Numerical types
       **/ 
      
      case EVR_SL:  // signed long
        return new DcmSignedLong(key);

      case EVR_SS:  // signed short
        return new DcmSignedShort(key);

      case EVR_UL:  // unsigned long
        return new DcmUnsignedLong(key);

      case EVR_US:  // unsigned short
        return new DcmUnsignedShort(key);

      case EVR_FL:  // float single-precision
        return new DcmFloatingPointSingle(key);

      case EVR_FD:  // float double-precision
        return new DcmFloatingPointDouble(key);


      /**
       * Sequence types, should never occur at this point.
       **/

      case EVR_SQ:  // sequence of items
        throw OrthancException(ErrorCode_ParameterOutOfRange);


      /**
       * Internal to DCMTK.
       **/ 

      case EVR_ox:  // OB or OW depending on context
      case EVR_xs:  // SS or US depending on context
      case EVR_lt:  // US, SS or OW depending on context, used for LUT Data (thus the name)
      case EVR_na:  // na="not applicable", for data which has no VR
      case EVR_up:  // up="unsigned pointer", used internally for DICOMDIR suppor
      case EVR_item:  // used internally for items
      case EVR_metainfo:  // used internally for meta info datasets
      case EVR_dataset:  // used internally for datasets
      case EVR_fileFormat:  // used internally for DICOM files
      case EVR_dicomDir:  // used internally for DICOMDIR objects
      case EVR_dirRecord:  // used internally for DICOMDIR records
      case EVR_pixelSQ:  // used internally for pixel sequences in a compressed image
      case EVR_pixelItem:  // used internally for pixel items in a compressed image
      case EVR_UNKNOWN: // used internally for elements with unknown VR (encoded with 4-byte length field in explicit VR)
      case EVR_PixelData:  // used internally for uncompressed pixeld data
      case EVR_OverlayData:  // used internally for overlay data
      case EVR_UNKNOWN2B:  // used internally for elements with unknown VR with 2-byte length field in explicit VR
      default:
        break;
    }

    throw OrthancException(ErrorCode_InternalError);          
  }



  void FromDcmtkBridge::FillElementWithString(DcmElement& element,
                                              const DicomTag& tag,
                                              const std::string& value,
                                              bool decodeBinaryTags)
  {
    std::string binary;
    const std::string* decoded = &value;

    if (decodeBinaryTags &&
        boost::starts_with(value, "data:application/octet-stream;base64,"))
    {
      std::string mime;
      Toolbox::DecodeDataUriScheme(mime, binary, value);
      decoded = &binary;
    }

    DcmTag key(tag.GetGroup(), tag.GetElement());

    if (IsBinaryTag(key))
    {
      if (element.putUint8Array((const Uint8*) decoded->c_str(), decoded->size()).good())
      {
        return;
      }
      else
      {
        throw OrthancException(ErrorCode_InternalError);
      }
    }

    bool ok = false;
    
    try
    {
      switch (key.getEVR())
      {
        // http://support.dcmtk.org/docs/dcvr_8h-source.html

        /**
         * TODO.
         **/

        case EVR_OB:  // other byte
        case EVR_OF:  // other float
        case EVR_OW:  // other word
        case EVR_AT:  // attribute tag
          throw OrthancException(ErrorCode_NotImplemented);
    
        case EVR_UN:  // unknown value representation
          throw OrthancException(ErrorCode_ParameterOutOfRange);


        /**
         * String types.
         **/
      
        case EVR_DS:  // decimal string
        case EVR_IS:  // integer string
        case EVR_AS:  // age string
        case EVR_DA:  // date string
        case EVR_DT:  // date time string
        case EVR_TM:  // time string
        case EVR_AE:  // application entity title
        case EVR_CS:  // code string
        case EVR_SH:  // short string
        case EVR_LO:  // long string
        case EVR_ST:  // short text
        case EVR_LT:  // long text
        case EVR_UT:  // unlimited text
        case EVR_PN:  // person name
        case EVR_UI:  // unique identifier
        {
          ok = element.putString(decoded->c_str()).good();
          break;
        }

        
        /**
         * Numerical types
         **/ 
      
        case EVR_SL:  // signed long
        {
          ok = element.putSint32(boost::lexical_cast<Sint32>(*decoded)).good();
          break;
        }

        case EVR_SS:  // signed short
        {
          ok = element.putSint16(boost::lexical_cast<Sint16>(*decoded)).good();
          break;
        }

        case EVR_UL:  // unsigned long
        {
          ok = element.putUint32(boost::lexical_cast<Uint32>(*decoded)).good();
          break;
        }

        case EVR_US:  // unsigned short
        {
          ok = element.putUint16(boost::lexical_cast<Uint16>(*decoded)).good();
          break;
        }

        case EVR_FL:  // float single-precision
        {
          ok = element.putFloat32(boost::lexical_cast<float>(*decoded)).good();
          break;
        }

        case EVR_FD:  // float double-precision
        {
          ok = element.putFloat64(boost::lexical_cast<double>(*decoded)).good();
          break;
        }


        /**
         * Sequence types, should never occur at this point.
         **/

        case EVR_SQ:  // sequence of items
        {
          ok = false;
          break;
        }


        /**
         * Internal to DCMTK.
         **/ 

        case EVR_ox:  // OB or OW depending on context
        case EVR_xs:  // SS or US depending on context
        case EVR_lt:  // US, SS or OW depending on context, used for LUT Data (thus the name)
        case EVR_na:  // na="not applicable", for data which has no VR
        case EVR_up:  // up="unsigned pointer", used internally for DICOMDIR suppor
        case EVR_item:  // used internally for items
        case EVR_metainfo:  // used internally for meta info datasets
        case EVR_dataset:  // used internally for datasets
        case EVR_fileFormat:  // used internally for DICOM files
        case EVR_dicomDir:  // used internally for DICOMDIR objects
        case EVR_dirRecord:  // used internally for DICOMDIR records
        case EVR_pixelSQ:  // used internally for pixel sequences in a compressed image
        case EVR_pixelItem:  // used internally for pixel items in a compressed image
        case EVR_UNKNOWN: // used internally for elements with unknown VR (encoded with 4-byte length field in explicit VR)
        case EVR_PixelData:  // used internally for uncompressed pixeld data
        case EVR_OverlayData:  // used internally for overlay data
        case EVR_UNKNOWN2B:  // used internally for elements with unknown VR with 2-byte length field in explicit VR
        default:
          break;
      }
    }
    catch (boost::bad_lexical_cast&)
    {
      ok = false;
    }

    if (!ok)
    {
      throw OrthancException(ErrorCode_InternalError);
    }
  }


  DcmElement* FromDcmtkBridge::FromJson(const DicomTag& tag,
                                        const Json::Value& value,
                                        bool decodeBinaryTags)
  {
    std::auto_ptr<DcmElement> element;

    switch (value.type())
    {
      case Json::stringValue:
        element.reset(CreateElementForTag(tag));
        FillElementWithString(*element, tag, value.asString(), decodeBinaryTags);
        break;

      case Json::arrayValue:
      {
        DcmTag key(tag.GetGroup(), tag.GetElement());
        if (key.getEVR() != EVR_SQ)
        {
          throw OrthancException(ErrorCode_BadParameterType);
        }

        DcmSequenceOfItems* sequence = new DcmSequenceOfItems(key, value.size());
        element.reset(sequence);
        
        for (Json::Value::ArrayIndex i = 0; i < value.size(); i++)
        {
          std::auto_ptr<DcmItem> item(new DcmItem);

          Json::Value::Members members = value[i].getMemberNames();
          for (Json::Value::ArrayIndex j = 0; j < members.size(); j++)
          {
            item->insert(FromJson(ParseTag(members[j]), value[i][members[j]], decodeBinaryTags));
          }

          sequence->append(item.release());
        }

        break;
      }

      default:
        throw OrthancException(ErrorCode_BadParameterType);
    }

    return element.release();
  }
}
