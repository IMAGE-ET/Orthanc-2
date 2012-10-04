/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012 Medical Physics Department, CHU of Liege,
 * Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
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

#include "DicomValue.h"
#include "DicomTag.h"

namespace Orthanc
{
  class DicomElement : public boost::noncopyable
  {
  private:
    DicomTag tag_;
    DicomValue* value_;

  public:
    DicomElement(uint16_t group,
                 uint16_t element,
                 const DicomValue& value) :
      tag_(group, element),
      value_(value.Clone())
    {
    }

    DicomElement(const DicomTag& tag,
                 const DicomValue& value) :
      tag_(tag),
      value_(value.Clone())
    {
    }

    ~DicomElement()
    {
      delete value_;
    }

    const DicomTag& GetTag() const
    {
      return tag_;
    }

    const DicomValue& GetValue() const
    {
      return *value_;
    }

    uint16_t GetTagGroup() const
    {
      return tag_.GetGroup();
    }

    uint16_t GetTagElement() const
    {
      return tag_.GetElement();
    }

    bool operator< (const DicomElement& other) const
    {
      return GetTag() < other.GetTag();
    }
  };
}
