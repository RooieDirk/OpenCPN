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
 *
 *
 */

#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
#include "wx/wx.h"
#endif //precompiled headers
#include "dychart.h"

#include "chcanv.h"
#include "trialmanoeuvre.h"
#include "chartdb.h"
#include "chart1.h"
#include "chartbase.h"
#include "styles.h"
#include "ocpndc.h"
#include "cutil.h"
#include "wx28compat.h"
#include "navutil.h"
#include "georef.h"
#include "AIS_Target_Data.h"
#include "AIS_Decoder.h"
#include "Select.h"
#ifdef __OCPN__ANDROID__
#include "androidUTIL.h"
#endif


//------------------------------------------------------------------------------
//    External Static Storage
//------------------------------------------------------------------------------

extern ocpnStyle::StyleManager  *g_StyleManager;
extern MyFrame                  *gFrame;
extern bool                     g_btouch;
extern int                      g_GUIScaleFactor;

extern ChartCanvas              *cc1;
extern TrialManoeuvreWin        *g_TrialManoeuvreWin;
extern double                   gLat, gLon, gCog, gSog, gHdt, g_ownship_HDTpredictor_miles;
extern AIS_Decoder              *g_pAIS;
extern Select                   *pSelect;
extern Select                   *pSelectAIS;

const long TrialManoeuvreWin::ID_SPEEDCTRL = wxNewId();
const long TrialManoeuvreWin::ID_SLIDERTEXT = wxNewId();
const long TrialManoeuvreWin::ID_SLIDER = wxNewId();
const long TrialManoeuvreWin::ID_COURSECTRL = wxNewId();
//------------------------------------------------------------------------------
//    TrialManoeuvreWin Implementation
//------------------------------------------------------------------------------
BEGIN_EVENT_TABLE(TrialManoeuvreWin, wxDialog)
    EVT_PAINT(TrialManoeuvreWin::OnPaint)
    EVT_SIZE(TrialManoeuvreWin::OnSize)
    EVT_MOUSE_EVENTS(TrialManoeuvreWin::MouseEvent)
END_EVENT_TABLE()

void TrialShipDraw( ocpnDC& dc )
{
    
}
TrialManoeuvreWin::TrialManoeuvreWin( wxWindow *win )
{

    long wstyle = wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR;
#ifndef __WXMAC__
    wstyle |= wxFRAME_SHAPED;
#endif
#ifdef __WXMAC__
    wstyle |= wxSTAY_ON_TOP;
#endif
    td_try = new AIS_Target_Data();
    //Preset variables
    MinutesToCC = 0;
    TimeOfCC = wxDateTime::Now();
    

    wxDialog::Create( win, wxID_ANY, _T(""), wxPoint( 0, 0 ), wxSize( win->m_clientWidth, 25 ), wstyle );
    SetBackgroundColour( GetGlobalColor( _T("URED") ) );
    SetBackgroundStyle( wxBG_STYLE_CUSTOM ); // on WXMSW, this prevents flashing on color scheme change

    //   Create the Children
    wxFloatingPointValidator<double> CrsVal(0, &CogTrial, wxNUM_VAL_ZERO_AS_BLANK);
        CrsVal.SetRange(0, 360);
        
    wxFloatingPointValidator<double> SpdVal(1, &SogTrial, wxNUM_VAL_ZERO_AS_BLANK);
        SpdVal.SetRange(0, 100);
    SpeedStatTxt = new wxStaticText(this, wxNewId(), getUsrSpeedUnit(), wxPoint(0,0), wxDefaultSize, 0, _T("ID_STATICTEXT1"));   
    SpeedCtrl = new wxTextCtrl(this, ID_SPEEDCTRL, wxT("0"), wxPoint(0,0), wxSize(40,20), wxWS_EX_VALIDATE_RECURSIVELY, 
                               SpdVal);
    SpeedCtrl->SetMaxLength(4);
    
    CourseStatTxt = new wxStaticText(this, wxNewId(), _("°"), wxPoint(0,0), wxDefaultSize, 0, _T("ID_STATICTEXT1"));
    CourseCtrl = new wxTextCtrl(this, ID_COURSECTRL, wxT("0"), wxPoint(0,0), wxSize(40,20), 0, CrsVal);   
    
    SliderTxt = new wxStaticText(this, ID_SLIDERTEXT, wxT("Coursechange in 45 mins, 20:56"), wxPoint(0,0), wxDefaultSize, 0, _T("ID_STATICTEXT1"));
    SetSliderString();
    Slider = new wxSlider(this, ID_SLIDER, 0, 0, 100, wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, _T("ID_SLIDER"));
    ReSize();
    // Connect Events
    Slider->Connect( wxEVT_SCROLL_TOP, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Connect( wxEVT_SCROLL_BOTTOM, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Connect( wxEVT_SCROLL_LINEUP, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Connect( wxEVT_SCROLL_LINEDOWN, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Connect( wxEVT_SCROLL_PAGEUP, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Connect( wxEVT_SCROLL_PAGEDOWN, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Connect( wxEVT_SCROLL_THUMBTRACK, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Connect( wxEVT_SCROLL_THUMBRELEASE, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Connect( wxEVT_SCROLL_CHANGED, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    SpeedCtrl->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( TrialManoeuvreWin::OnTextSpeed ), NULL, this );
    CourseCtrl->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( TrialManoeuvreWin::OnTextCourse ), NULL, this );
    
}

TrialManoeuvreWin::~TrialManoeuvreWin()
{
    // Disconnect Events
    Slider->Disconnect( wxEVT_SCROLL_TOP, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Disconnect( wxEVT_SCROLL_BOTTOM, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Disconnect( wxEVT_SCROLL_LINEUP, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Disconnect( wxEVT_SCROLL_LINEDOWN, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Disconnect( wxEVT_SCROLL_PAGEUP, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Disconnect( wxEVT_SCROLL_PAGEDOWN, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Disconnect( wxEVT_SCROLL_THUMBTRACK, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Disconnect( wxEVT_SCROLL_THUMBRELEASE, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    Slider->Disconnect( wxEVT_SCROLL_CHANGED, wxScrollEventHandler( TrialManoeuvreWin::OnScroll ), NULL, this );
    SpeedCtrl->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( TrialManoeuvreWin::OnTextSpeed ), NULL, this );
    CourseCtrl->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( TrialManoeuvreWin::OnTextCourse ), NULL, this );
    delete td_try;
}

void TrialManoeuvreWin::RePosition()
{
    wxSize cs = GetParent()->GetClientSize();
    wxFrame *frame = dynamic_cast<wxFrame*>(GetParent());
    wxPoint position;
    position.x = 0;
    position.y = cs.y;
    position.y -= GetSize().y;

    wxPoint screen_pos = GetParent()->ClientToScreen( position );
    Move( screen_pos );
#ifdef __OCPN__ANDROID__
    Raise();
#endif
}

void TrialManoeuvreWin::ReSize()
{
    wxSize cs = GetParent()->GetClientSize();
    this->SetSize(0,0,cs.x,this->GetSize().y);
    SpeedStatTxt->SetPosition(wxPoint( GetParent()->GetClientSize().x - SpeedStatTxt->m_width - 5, 0));
    SpeedCtrl->SetPosition(wxPoint(SpeedStatTxt->GetPosition().x - SpeedCtrl->m_width-5, 0));
    CourseStatTxt->SetPosition( wxPoint(SpeedCtrl->GetPosition().x - CourseStatTxt->m_width -5, 0));
    CourseCtrl->SetPosition(wxPoint(CourseStatTxt->GetPosition().x - CourseCtrl->m_width -5, 0));
    SliderTxt->SetPosition( wxPoint(CourseCtrl->GetPosition().x - SliderTxt->m_width -5, 0));
    Slider->SetSize(0, 0, (SliderTxt->GetPosition().x -5),this->GetClientSize().y-4);
//    Raise();    
}

void TrialManoeuvreWin::OnPaint( wxPaintEvent& event )
{
    wxPaintDC dc( this );
}

void TrialManoeuvreWin::OnSize( wxSizeEvent& event )
{
    if (!IsShown())
        return;
}

void TrialManoeuvreWin::MouseEvent( wxMouseEvent& event )
{
    //g_Piano->MouseEvent( event );
}

void TrialManoeuvreWin::SetSliderString()
{
    SliderTxt->SetLabel(wxString::Format(wxT("Coursechange in %02i min. %02i:%02i "),
                                  MinutesToCC, TimeOfCC.GetHour(), TimeOfCC.GetMinute() ));
}
void TrialManoeuvreWin::OnScroll( wxScrollEvent& event )
{ 
    MinutesToCC = Slider->GetValue();
    TimeOfCC = wxDateTime::Now()+ wxTimeSpan( 0, MinutesToCC);
    SetSliderString();
    RecalcPos();
    
    event.Skip();     
}
void TrialManoeuvreWin::OnTextSpeed(wxCommandEvent& event)
{
    SpeedCtrl->Validate();//  SpdVal->Validate();
    if(!SpeedCtrl->GetValue().ToDouble(&SogTrial)){ /* error! */ };
    SaveSetGlobalVar(true);
 //   printf("ontextSpeed %f\n",SogTrial);
}
void TrialManoeuvreWin::OnTextCourse(wxCommandEvent& event)
{
    CourseCtrl->Validate();//  SpdVal->Validate();
    if(!CourseCtrl->GetValue().ToDouble(&CogTrial)){ /* error! */ };
    SaveSetGlobalVar(true);
    
//    printf("ontextCourse %f\n",CogTrial);
}
void TrialManoeuvreWin::RecalcPos()
{
    ll_gc_ll(gLat, gLon, gCog, gSog*MinutesToCC/60, &LatTrial, &LonTrial);
           //    Iterate thru all the targets
    AIS_Target_Hash::iterator it;
    AIS_Target_Hash *current_targets = g_pAIS->GetTargetList();

    for( it = ( *current_targets ).begin(); it != ( *current_targets ).end(); ++it ) {
        AIS_Target_Data *td = it->second;
//        td->UpdateLatLon();
//        td->UpdateCPA();
        pSelect->ModifySelectablePoint( LatTrial, LonTrial, (void *)(long)td->MMSI, SELTYPE_AISTARGET );
    }
//printf(" lat %f  lon %f", LatTrial, LonTrial);    
}
bool TrialManoeuvreWin::doShow(bool show)
{
    LatTrial = gLat;
    LonTrial = gLon;
    SogTrial = gSog;
    CogTrial = gCog;
//     CourseCtrl->SetValue(wxString::Format(wxT("%03i"),(int)gCog));
//     SpeedCtrl->SetValue(wxString::Format(wxT("%.1f"),SogTrial)); 
    return Show(show);
   //(wxDialog)this->GetParent().Show(show);
}
void TrialManoeuvreWin::SaveSetGlobalVar(bool save)
{
    if ( save ){
        gLatTemp = gLat; // store some global vars
        gLonTemp = gLon;
        gCogTemp = gCog;
        gSogTemp = gSog;
        gHdtTemp = gHdt;
        g_ownship_HDTpredictor_milesTemp = g_ownship_HDTpredictor_miles;
        
        gLat = LatTrial;
        gLon = LonTrial;
        gCog = CogTrial;
        gSog = SogTrial;
        g_ownship_HDTpredictor_miles = 0.;
        gHdt = gCog;
    }
    else{    
        gLat = gLatTemp; // set back some global vars
        gLon = gLonTemp;
        gCog = gCogTemp;
        gSog = gSogTemp;
        g_ownship_HDTpredictor_miles = g_ownship_HDTpredictor_milesTemp;
        gHdt = gHdtTemp;
    }
    //RecalcPos();
}

void TrialManoeuvreWin::SaveSetAISTarget(AIS_Target_Data *td, bool save)
{
    if ( save){
        TargetLatTemp = td->Lat;
        TargetLonTemp = td->Lon;
        ll_gc_ll(TargetLatTemp, TargetLonTemp, td->COG, td->SOG *MinutesToCC/60, &td->Lat, &td->Lon);
    }
    else{ //set back
        td->Lat = TargetLatTemp;
        td->Lon = TargetLonTemp;
    }
}
AIS_Target_Data* TrialManoeuvreWin::SetTrialAisData( AIS_Target_Data* orgtd )
{
    CalcOneCPA( orgtd ); //calc cpa with trialcourse and speed
    td_try->CloneFrom(orgtd);
    double lat, lon;
    ll_gc_ll(td_try->Lat, td_try->Lon, td_try->COG,td_try->SOG * MinutesToCC/60, &lat, &lon);
    td_try->Lat = lat;
    td_try->Lon = lon;

    long mmsi_long = orgtd->MMSI;
    SelectItem *pSel = pSelectAIS->AddSelectablePoint( lat,
                       lon, (void *) mmsi_long, SELTYPE_AISTARGET );
    pSel->SetUserData( orgtd->MMSI );
    
    return td_try;
}

void TrialManoeuvreWin::CalcOneCPA( AIS_Target_Data *ptarget )
{
    ptarget->Range_NM = -1.;            // Defaults
    ptarget->Brg = -1.;

    //    Compute the current Range/Brg to the target
    double brg, dist;
    DistanceBearingMercator( ptarget->Lat, ptarget->Lon, LatTrial, LonTrial, &brg, &dist );
    ptarget->Range_NM = dist;
    ptarget->Brg = brg;

    if( dist <= 1e-5 ) ptarget->Brg = -1.0;             // Brg is undefined if Range == 0.

    //    There can be no collision between ownship and itself....
    //    This can happen if AIVDO messages are received, and there is another source of ownship position, like NMEA GLL
    //    The two positions are always temporally out of sync, and one will always be exactly in front of the other one.
    if( ptarget->b_OwnShip ) {
        ptarget->CPA = 100;
        ptarget->TCPA = -100;
        ptarget->bCPA_Valid = false;
        return;
    }

    double cpa_calc_ownship_cog = CogTrial;
    double cpa_calc_target_cog = ptarget->COG;

//    Ownship is not reporting valid SOG, so no way to calculate CPA
    if( wxIsNaN(SogTrial) || ( SogTrial > 102.2 ) ) {
        ptarget->bCPA_Valid = false;
        return;
    }

//    Ownship is maybe anchored and not reporting COG
    if( wxIsNaN(CogTrial) || CogTrial == 360.0 ) {
        if( SogTrial < .01 ) cpa_calc_ownship_cog = 0.;          // substitute value
                                                             // for the case where SOG ~= 0, and COG is unknown.
        else {
            ptarget->bCPA_Valid = false;
            return;
        }
    }

//    Target is maybe anchored and not reporting COG
    if( ptarget->COG == 360.0 ) {
        if( ptarget->SOG > 102.2 ) {
            ptarget->bCPA_Valid = false;
            return;
        } else if( ptarget->SOG < .01 ) cpa_calc_target_cog = 0.;           // substitute value
                                                                            // for the case where SOG ~= 0, and COG is unknown.
        else {
            ptarget->bCPA_Valid = false;
            return;
        }
    }

    //    Express the SOGs as meters per hour
    double v0 = SogTrial * 1852.;
    double v1 = ptarget->SOG * 1852.;

    if( ( v0 < 1e-6 ) && ( v1 < 1e-6 ) ) {
        ptarget->TCPA = 0.;
        ptarget->CPA = 0.;

        ptarget->bCPA_Valid = false;
    } else {
        //    Calculate the TCPA first

        //    Working on a Reduced Lat/Lon orthogonal plotting sheet....
        //    Get easting/northing to target,  in meters

        double east1 = ( ptarget->Lon - LonTrial ) * 60 * 1852;
        double north1 = ( ptarget->Lat - LatTrial ) * 60 * 1852;

        double east = east1 * ( cos( LatTrial * PI / 180. ) );
        
        double north = north1;

        //    Convert COGs trigonometry to standard unit circle
        double cosa = cos( ( 90. - cpa_calc_ownship_cog ) * PI / 180. );
        double sina = sin( ( 90. - cpa_calc_ownship_cog ) * PI / 180. );
        double cosb = cos( ( 90. - cpa_calc_target_cog ) * PI / 180. );
        double sinb = sin( ( 90. - cpa_calc_target_cog ) * PI / 180. );

        //    These will be useful
        double fc = ( v0 * cosa ) - ( v1 * cosb );
        double fs = ( v0 * sina ) - ( v1 * sinb );

        double d = ( fc * fc ) + ( fs * fs );
        double tcpa;

        // the tracks are almost parallel
        if( fabs( d ) < 1e-6 ) tcpa = 0.;
        else
            //    Here is the equation for t, which will be in hours
            tcpa = ( ( fc * east ) + ( fs * north ) ) / d;

        //    Convert to minutes
        ptarget->TCPA = tcpa * 60.;

        //    Calculate CPA
        //    Using TCPA, predict ownship and target positions

        double OwnshipLatCPA, OwnshipLonCPA, TargetLatCPA, TargetLonCPA;

        ll_gc_ll( LatTrial, LonTrial, cpa_calc_ownship_cog, SogTrial * tcpa, &OwnshipLatCPA, &OwnshipLonCPA );
        ll_gc_ll( ptarget->Lat, ptarget->Lon, cpa_calc_target_cog, ptarget->SOG * tcpa,
                &TargetLatCPA, &TargetLonCPA );

        //   And compute the distance
        ptarget->CPA = DistGreatCircle( OwnshipLatCPA, OwnshipLonCPA, TargetLatCPA, TargetLonCPA );
        ptarget->bCPA_Valid = true;

        if( ptarget->TCPA < 0 ) ptarget->bCPA_Valid = false;
    }
}