/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Chart Bar Window
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 */


#ifndef __trialwin_H__
#define __trialwin_H__

#include "chart1.h"
#include <wx/valnum.h>
#include "AIS_Target_Data.h"

//----------------------------------------------------------------------------
// TrialManoeuvreWin
//----------------------------------------------------------------------------
class TrialManoeuvreWin: public wxDialog
{
public:
    TrialManoeuvreWin(wxWindow *win);
    ~TrialManoeuvreWin();
    void OnSize(wxSizeEvent& event);
    void OnPaint(wxPaintEvent& event);
    void MouseEvent(wxMouseEvent& event);
    int  GetFontHeight();
    void RePosition();
    void ReSize();
    void RecalcPos();
    bool doShow(bool show = 1);
    void SaveSetGlobalVar(bool save);
    void SaveSetAISTarget(AIS_Target_Data *td, bool save);
    void CalcOneCPA( AIS_Target_Data *ptarget );
    AIS_Target_Data* SetTrialAisData( AIS_Target_Data* orgtd );
    wxStaticText* SpeedStatTxt;
    wxTextCtrl* SpeedCtrl; 
    wxStaticText* CourseStatTxt;
    //(*Declarations(NewFrame)
    wxStaticText* SliderTxt;
    wxTextCtrl* CourseCtrl;
    wxStaticText* StaticText92;
    wxSlider* Slider;
    int MinutesToCC;
    wxDateTime TimeOfCC;
    double LatTrial;
    double LonTrial;
    double CogTrial;
    double SogTrial;
    AIS_Target_Data *td_try;
protected:
    static const long ID_SLIDER;
    static const long ID_SLIDERTEXT;
    static const long ID_COURSECTRL;
    static const long ID_SPEEDCTRL;
    double mSpdTxtValue;
    double mCrsTxtValue;
//    wxFloatingPointValidator SpdVal;
    
    virtual void OnScroll( wxScrollEvent& event );
    void OnTextSpeed(wxCommandEvent& event);
    void OnTextCourse(wxCommandEvent& event);

    void SetSliderString();
    double gLatTemp, gLonTemp, gCogTemp, gSogTemp, g_ownship_HDTpredictor_milesTemp, gHdtTemp;
    double TargetLatTemp, TargetLonTemp;
        //*)      
DECLARE_EVENT_TABLE();
};

#endif
