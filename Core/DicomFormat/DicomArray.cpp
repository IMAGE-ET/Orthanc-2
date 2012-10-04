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


#include "DicomArray.h"

#include <stdio.h>

namespace Orthanc
{
  DicomArray::DicomArray(const DicomMap& map)
  {
    elements_.reserve(map.map_.size());
    
    for (DicomMap::Map::const_iterator it = 
           map.map_.begin(); it != map.map_.end(); it++)
    {
      elements_.push_back(new DicomElement(it->first, *it->second));
    }
  }


  DicomArray::~DicomArray()
  {
    for (size_t i = 0; i < elements_.size(); i++)
    {
      delete elements_[i];
    }
  }


  void DicomArray::Print(FILE* fp) const
  {
    for (size_t  i = 0; i < elements_.size(); i++)
    {
      DicomTag t = elements_[i]->GetTag();
      std::string s = elements_[i]->GetValue().AsString();
      printf("0x%04x 0x%04x [%s]\n", t.GetGroup(), t.GetElement(), s.c_str());
    }
  }
}
