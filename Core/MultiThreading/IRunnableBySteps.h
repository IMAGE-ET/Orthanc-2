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

namespace Orthanc
{
  class IRunnableBySteps
  {
  public:
    virtual ~IRunnableBySteps()
    {
    }

    // Must return "true" if the runnable wishes to continue. Must
    // return "false" if the runnable has not finished its job.
    virtual bool Step() = 0;

    static void RunUntilDone(IRunnableBySteps& runnable)
    {
      while (runnable.Step());
    }
  };
}
