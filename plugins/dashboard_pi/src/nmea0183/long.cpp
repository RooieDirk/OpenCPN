/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  NMEA0183 Support Classes
 * Author:   Samuel R. Blackburn, David S. Register
 *
 ***************************************************************************
 *   Copyright (C) 2010 by Samuel R. Blackburn, David S Register           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.             *
 ***************************************************************************
 *
 *   S Blackburn's original source license:                                *
 *         "You can use it any way you like."                              *
 *   More recent (2010) license statement:                                 *
 *         "It is BSD license, do with it what you will"                   *
 */

#include "nmea0183.h"

/*
** Author: Samuel R. Blackburn
** CI$: 76300,326
** Internet: sammy@sed.csc.com
**
** You can use it any way you like.
*/

LONGITUDE::LONGITUDE() { Empty(); }

LONGITUDE::~LONGITUDE() { Empty(); }

void LONGITUDE::Empty(void) {
  Longitude = 0.0;
  Easting = EW_Unknown;
}

bool LONGITUDE::IsDataValid(void) {
  if (Easting != East && Easting != West) {
    return (FALSE);
  }

  return (TRUE);
}

void LONGITUDE::Parse(int position_field_number, int east_or_west_field_number,
                      const SENTENCE& sentence) {
  wxString w_or_e = sentence.Field(east_or_west_field_number);
  Set(sentence.Double(position_field_number), w_or_e);
}

void LONGITUDE::Set(double position, const wxString& east_or_west) {
  //   assert( east_or_west != NULL );

  Longitude = position;
  wxString ts = east_or_west;

  if (!ts.IsEmpty() && ts.Trim(false)[0] == 'E') {
    Easting = East;
  } else if (!ts.IsEmpty() && ts.Trim(false)[0] == 'W') {
    Easting = West;
  } else {
    Easting = EW_Unknown;
  }
}

void LONGITUDE::Write(SENTENCE& sentence) {
  wxString temp_string;
  int neg = 0;
  int d;
  int m;

  if (Longitude < 0.0) {
    Longitude = -Longitude;
    neg = 1;
  }
  d = (int)Longitude;
  m = (int)((Longitude - (double)d) * 60000.0);

  if (neg) d = -d;

  temp_string.Printf(_T("%03d%02d.%03d"), d, m / 1000, m % 1000);

  sentence += temp_string;

  if (Easting == East) {
    sentence += _T("E");
  } else if (Easting == West) {
    sentence += _T("W");
  } else {
    /*
    ** Add Nothing
    */
  }
}

const LONGITUDE& LONGITUDE::operator=(const LONGITUDE& source) {
  Longitude = source.Longitude;
  Easting = source.Easting;

  return (*this);
}
