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


#include "../PrecompiledHeaders.h"
#include "ImageAccessor.h"

#include "../OrthancException.h"
#include "../ChunkedBuffer.h"

#include <stdint.h>
#include <cassert>
#include <glog/logging.h>
#include <boost/lexical_cast.hpp>

namespace Orthanc
{
  template <typename PixelType>
  static void ToMatlabStringInternal(ChunkedBuffer& target,
                                     const ImageAccessor& source)
  {
    target.AddChunk("double([ ");

    for (unsigned int y = 0; y < source.GetHeight(); y++)
    {
      const PixelType* p = reinterpret_cast<const PixelType*>(source.GetConstRow(y));

      std::string s;
      if (y > 0)
      {
        s = "; ";
      }

      s.reserve(source.GetWidth() * 8);

      for (unsigned int x = 0; x < source.GetWidth(); x++, p++)
      {
        s += boost::lexical_cast<std::string>(static_cast<int>(*p)) + " ";
      }

      target.AddChunk(s);
    }

    target.AddChunk("])");
  }


  static void RGB24ToMatlabString(ChunkedBuffer& target,
                                  const ImageAccessor& source)
  {
    assert(source.GetFormat() == PixelFormat_RGB24);

    target.AddChunk("double(permute(reshape([ ");

    for (unsigned int y = 0; y < source.GetHeight(); y++)
    {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(source.GetConstRow(y));
      
      std::string s;
      s.reserve(source.GetWidth() * 3 * 8);
      
      for (unsigned int x = 0; x < 3 * source.GetWidth(); x++, p++)
      {
        s += boost::lexical_cast<std::string>(static_cast<int>(*p)) + " ";
      }
      
      target.AddChunk(s);
    }

    target.AddChunk("], [ 3 " + boost::lexical_cast<std::string>(source.GetHeight()) +
                    " " + boost::lexical_cast<std::string>(source.GetWidth()) + " ]), [ 3 2 1 ]))");
  }


  void* ImageAccessor::GetBuffer() const
  {
    if (readOnly_)
    {
      LOG(ERROR) << "Trying to write on a read-only image";
      throw OrthancException(ErrorCode_ReadOnly);
    }

    return buffer_;
  }


  const void* ImageAccessor::GetConstRow(unsigned int y) const
  {
    if (buffer_ != NULL)
    {
      return reinterpret_cast<const uint8_t*>(buffer_) + y * pitch_;
    }
    else
    {
      return NULL;
    }
  }


  void* ImageAccessor::GetRow(unsigned int y) const
  {
    if (readOnly_)
    {
      LOG(ERROR) << "Trying to write on a read-only image";
      throw OrthancException(ErrorCode_ReadOnly);
    }

    if (buffer_ != NULL)
    {
      return reinterpret_cast<uint8_t*>(buffer_) + y * pitch_;
    }
    else
    {
      return NULL;
    }
  }


  void ImageAccessor::AssignEmpty(PixelFormat format)
  {
    readOnly_ = false;
    format_ = format;
    width_ = 0;
    height_ = 0;
    pitch_ = 0;
    buffer_ = NULL;
  }


  void ImageAccessor::AssignReadOnly(PixelFormat format,
                                     unsigned int width,
                                     unsigned int height,
                                     unsigned int pitch,
                                     const void *buffer)
  {
    readOnly_ = true;
    format_ = format;
    width_ = width;
    height_ = height;
    pitch_ = pitch;
    buffer_ = const_cast<void*>(buffer);

    assert(GetBytesPerPixel(format_) * width_ <= pitch_);
  }


  void ImageAccessor::AssignWritable(PixelFormat format,
                                     unsigned int width,
                                     unsigned int height,
                                     unsigned int pitch,
                                     void *buffer)
  {
    readOnly_ = false;
    format_ = format;
    width_ = width;
    height_ = height;
    pitch_ = pitch;
    buffer_ = buffer;

    assert(GetBytesPerPixel(format_) * width_ <= pitch_);
  }


  void ImageAccessor::ToMatlabString(std::string& target) const
  {
    ChunkedBuffer buffer;

    switch (GetFormat())
    {
      case PixelFormat_Grayscale8:
        ToMatlabStringInternal<uint8_t>(buffer, *this);
        break;

      case PixelFormat_Grayscale16:
        ToMatlabStringInternal<uint16_t>(buffer, *this);
        break;

      case PixelFormat_SignedGrayscale16:
        ToMatlabStringInternal<int16_t>(buffer, *this);
        break;

      case PixelFormat_RGB24:
        RGB24ToMatlabString(buffer, *this);
        break;

      default:
        throw OrthancException(ErrorCode_NotImplemented);
    }   

    buffer.Flatten(target);
  }

}
