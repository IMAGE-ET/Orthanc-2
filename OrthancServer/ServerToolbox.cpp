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
#include "ServerToolbox.h"

#include "../Core/DicomFormat/DicomArray.h"
#include "../Core/FileStorage/StorageAccessor.h"
#include "../Core/Logging.h"
#include "../Core/OrthancException.h"
#include "ParsedDicomFile.h"

#include <cassert>

namespace Orthanc
{
  namespace Toolbox
  {
    void SimplifyTags(Json::Value& target,
                      const Json::Value& source)
    {
      assert(source.isObject());

      target = Json::objectValue;
      Json::Value::Members members = source.getMemberNames();

      for (size_t i = 0; i < members.size(); i++)
      {
        const Json::Value& v = source[members[i]];
        const std::string& name = v["Name"].asString();
        const std::string& type = v["Type"].asString();

        if (type == "String")
        {
          target[name] = v["Value"].asString();
        }
        else if (type == "TooLong" ||
                 type == "Null")
        {
          target[name] = Json::nullValue;
        }
        else if (type == "Sequence")
        {
          const Json::Value& array = v["Value"];
          assert(array.isArray());

          Json::Value children = Json::arrayValue;
          for (Json::Value::ArrayIndex i = 0; i < array.size(); i++)
          {
            Json::Value c;
            SimplifyTags(c, array[i]);
            children.append(c);
          }

          target[name] = children;
        }
        else
        {
          assert(0);
        }
      }
    }


    void LogMissingRequiredTag(const DicomMap& summary)
    {
      std::string s, t;

      if (summary.HasTag(DICOM_TAG_PATIENT_ID))
      {
        if (t.size() > 0)
          t += ", ";
        t += "PatientID=" + summary.GetValue(DICOM_TAG_PATIENT_ID).AsString();
      }
      else
      {
        if (s.size() > 0)
          s += ", ";
        s += "PatientID";
      }

      if (summary.HasTag(DICOM_TAG_STUDY_INSTANCE_UID))
      {
        if (t.size() > 0)
          t += ", ";
        t += "StudyInstanceUID=" + summary.GetValue(DICOM_TAG_STUDY_INSTANCE_UID).AsString();
      }
      else
      {
        if (s.size() > 0)
          s += ", ";
        s += "StudyInstanceUID";
      }

      if (summary.HasTag(DICOM_TAG_SERIES_INSTANCE_UID))
      {
        if (t.size() > 0)
          t += ", ";
        t += "SeriesInstanceUID=" + summary.GetValue(DICOM_TAG_SERIES_INSTANCE_UID).AsString();
      }
      else
      {
        if (s.size() > 0)
          s += ", ";
        s += "SeriesInstanceUID";
      }

      if (summary.HasTag(DICOM_TAG_SOP_INSTANCE_UID))
      {
        if (t.size() > 0)
          t += ", ";
        t += "SOPInstanceUID=" + summary.GetValue(DICOM_TAG_SOP_INSTANCE_UID).AsString();
      }
      else
      {
        if (s.size() > 0)
          s += ", ";
        s += "SOPInstanceUID";
      }

      if (t.size() == 0)
      {
        LOG(ERROR) << "Store has failed because all the required tags (" << s << ") are missing (is it a DICOMDIR file?)";
      }
      else
      {
        LOG(ERROR) << "Store has failed because required tags (" << s << ") are missing for the following instance: " << t;
      }
    }


    void SetMainDicomTags(IDatabaseWrapper& database,
                          int64_t resource,
                          ResourceType level,
                          const DicomMap& dicomSummary,
                          bool includeIdentifiers)
    {
      // WARNING: The database should be locked with a transaction!

      DicomMap tags;

      switch (level)
      {
        case ResourceType_Patient:
          dicomSummary.ExtractPatientInformation(tags);
          break;

        case ResourceType_Study:
          dicomSummary.ExtractStudyInformation(tags);
          break;

        case ResourceType_Series:
          dicomSummary.ExtractSeriesInformation(tags);
          break;

        case ResourceType_Instance:
          dicomSummary.ExtractInstanceInformation(tags);
          break;

        default:
          throw OrthancException(ErrorCode_InternalError);
      }

      DicomArray flattened(tags);
      for (size_t i = 0; i < flattened.GetSize(); i++)
      {
        const DicomElement& element = flattened.GetElement(i);

        if (includeIdentifiers ||
            !element.GetTag().IsIdentifier())
        {
          database.SetMainDicomTag(resource, element.GetTag(), element.GetValue().AsString());
        }
      }
    }


    bool FindOneChildInstance(int64_t& result,
                              IDatabaseWrapper& database,
                              int64_t resource,
                              ResourceType type)
    {
      for (;;)
      {
        if (type == ResourceType_Instance)
        {
          result = resource;
          return true;
        }

        std::list<int64_t> children;
        database.GetChildrenInternalId(children, resource);
        if (children.empty())
        {
          return false;
        }

        resource = children.front();
        type = GetChildResourceType(type);    
      }
    }


    void ReconstructMainDicomTags(IDatabaseWrapper& database,
                                  IStorageArea& storageArea,
                                  ResourceType level)
    {
      // WARNING: The database should be locked with a transaction!

      const char* plural = NULL;

      switch (level)
      {
        case ResourceType_Patient:
          plural = "patients";
          break;

        case ResourceType_Study:
          plural = "studies";
          break;

        case ResourceType_Series:
          plural = "series";
          break;

        case ResourceType_Instance:
          plural = "instances";
          break;

        default:
          throw OrthancException(ErrorCode_InternalError);
      }

      LOG(WARNING) << "Upgrade: Reconstructing the main DICOM tags of all the " << plural << "...";

      std::list<std::string> resources;
      database.GetAllPublicIds(resources, level);

      for (std::list<std::string>::const_iterator
             it = resources.begin(); it != resources.end(); it++)
      {
        // Locate the resource and one of its child instances
        int64_t resource, instance;
        ResourceType tmp;

        if (!database.LookupResource(resource, tmp, *it) ||
            tmp != level ||
            !FindOneChildInstance(instance, database, resource, level))
        {
          throw OrthancException(ErrorCode_InternalError);
        }

        // Get the DICOM file attached to some instances in the resource
        FileInfo attachment;
        if (!database.LookupAttachment(attachment, instance, FileContentType_Dicom))
        {
          throw OrthancException(ErrorCode_InternalError);
        }

        // Read and parse the content of the DICOM file
        StorageAccessor accessor(storageArea);

        std::string content;
        accessor.Read(content, attachment);

        ParsedDicomFile dicom(content);

        // Update the tags of this resource
        DicomMap dicomSummary;
        dicom.Convert(dicomSummary);

        database.ClearMainDicomTags(resource);

        switch (level)
        {
          case ResourceType_Patient:
            Toolbox::SetMainDicomTags(database, resource, ResourceType_Patient, dicomSummary, true);
            break;

          case ResourceType_Study:
          {
            Toolbox::SetMainDicomTags(database, resource, ResourceType_Study, dicomSummary, true);

            // Duplicate the patient tags at the study level (new in Orthanc 0.9.5 - db v6)
            Toolbox::SetMainDicomTags(database, resource, ResourceType_Patient, dicomSummary, false);

            DicomMap module;
            Toolbox::ExtractModule(module, dicomSummary, DicomModule_Patient, true  /* normalize */);
            Toolbox::ExtractModule(module, dicomSummary, DicomModule_Study, true  /* normalize */);
            database.StoreStudyModule(resource, module);

            break;
          }

          case ResourceType_Series:
            Toolbox::SetMainDicomTags(database, resource, ResourceType_Series, dicomSummary, true);
            break;

          case ResourceType_Instance:
            Toolbox::SetMainDicomTags(database, resource, ResourceType_Instance, dicomSummary, true);
            break;

          default:
            throw OrthancException(ErrorCode_InternalError);
        }
      }
    }


    void ExtractModule(DicomMap& result,   // WARNING: Will not be cleared!
                       const DicomMap& summary,
                       DicomModule module,
                       bool normalize)
    {
      typedef std::set<DicomTag> ModuleTags;
      ModuleTags moduleTags;
      DicomTag::AddTagsForModule(moduleTags, module);

      for (ModuleTags::const_iterator tag = moduleTags.begin(); tag != moduleTags.end(); ++tag)
      {
        const DicomValue* value = summary.TestAndGetValue(*tag);
        if (value != NULL &&
            !value->IsNull())
        {
          std::string t = value->AsString();

          if (normalize)
          {
            t = StripSpaces(ConvertToAscii(t));
            ToUpperCase(t);
          }

          result.SetValue(*tag, t);
        }      
      }
    }
  }
}
