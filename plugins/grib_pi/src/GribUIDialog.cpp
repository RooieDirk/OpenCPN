/***************************************************************************
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
 ***************************************************************************/
/**
 * \file
 * \implements \ref GribUIDialog.h
 */
#include "wx/wx.h"
#include "wx/tokenzr.h"
#include "wx/datetime.h"
#include "wx/sound.h"
#include <wx/wfstream.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/debug.h>
#include <wx/graphics.h>
#include <wx/regex.h>

#include <wx/stdpaths.h>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "ocpn_plugin.h"
#include "pi_gl.h"
#include "grib_pi.h"
#include "GribTable.h"
#include "email.h"
#include "folder.xpm"
#include "GribUIDialog.h"
#include <wx/arrimpl.cpp>

#ifdef __ANDROID__
#include "android_jvm.h"
#endif

// general variables
double m_cursor_lat, m_cursor_lon;
int m_Altitude;
int m_DialogStyle;
/**
 * Persisted version of the GRIB area selection mode.
 *
 * This value is saved to and loaded from the OpenCPN config file, allowing the
 * user's preferred selection mode to persist between sessions. The value is
 * used to:
 * - Restore the last used selection mode on startup.
 * - Keep track of the mode when dialog is closed/reopened.
 * - Revert to previous mode if changes are cancelled.
 */
int m_SavedZoneSelMode;
/**
 * Tracks the current state of GRIB area selection for zone coordinates.
 *
 * This state controls:
 * - Whether coordinates come from viewport or mouse area selection.
 * - When to display the selection overlay during drawing.
 * - When to update the lat/lon input fields.
 *
 * A typical mouse area selection flow:
 * 1. START_SELECTION when manual mode chosen.
 * 2. DRAW_SELECTION when user starts Shift+drag.
 * 3. COMPLETE_SELECTION when user releases mouse button.
 */
int m_ZoneSelMode;

extern grib_pi *g_pi;

#ifdef __MSVC__
#if _MSC_VER < 1700
int round(double x) {
  int i = (int)x;
  if (x >= 0.0) {
    return ((x - i) >= 0.5) ? (i + 1) : (i);
  } else {
    return (-x + i >= 0.5) ? (i - 1) : (i);
  }
}
#endif
#endif

#ifdef __WXQT__
#include "qdebug.h"
#endif

#if wxCHECK_VERSION(2, 9, 4) /* to work with wx 2.8 */
#define SetBitmapLabelLabel SetBitmap
#endif

#define DEFAULT_STYLE \
  = wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU | wxTAB_TRAVERSAL

WX_DEFINE_OBJARRAY(ArrayOfGribRecordSets);

//    Sort compare function for File Modification Time
static int CompareFileStringTime(const wxString &first,
                                 const wxString &second) {
  wxFileName f(first);
  wxFileName s(second);
  wxTimeSpan sp = s.GetModificationTime() - f.GetModificationTime();
  return sp.GetMinutes();

  //      return ::wxFileModificationTime(first) -
  //      ::wxFileModificationTime(second);
}

wxWindow *GetGRIBCanvas() {
  wxWindow *wx;
  // If multicanvas are active, render the overlay on the right canvas only
  if (GetCanvasCount() > 1)  // multi?
    wx = GetCanvasByIndex(1);
  else
    wx = GetOCPNCanvasWindow();
  return wx;
}

//---------------------------------------------------------------------------------------
//          GRIB Control Implementation
//---------------------------------------------------------------------------------------
/* interpolating constructor
   as a possible optimization, write this function to also
   take latitude longitude boundaries so the resulting record can be
   a subset of the input, but also would need to be recomputed when panning the
   screen */
GribTimelineRecordSet::GribTimelineRecordSet(unsigned int cnt)
    : GribRecordSet(cnt) {
  for (int i = 0; i < Idx_COUNT; i++) m_IsobarArray[i] = nullptr;
}

GribTimelineRecordSet::~GribTimelineRecordSet() {
  // RemoveGribRecords();
  ClearCachedData();
}

void GribTimelineRecordSet::ClearCachedData() {
  for (int i = 0; i < Idx_COUNT; i++) {
    if (m_IsobarArray[i]) {
      // Clear out the cached isobars
      for (unsigned int j = 0; j < m_IsobarArray[i]->GetCount(); j++) {
        IsoLine *piso = (IsoLine *)m_IsobarArray[i]->Item(j);
        delete piso;
      }
      delete m_IsobarArray[i];
      m_IsobarArray[i] = nullptr;
    }
  }
}

//---------------------------------------------------------------------------------------
//          GRIB CtrlBar Implementation
//---------------------------------------------------------------------------------------

GRIBUICtrlBar::GRIBUICtrlBar(wxWindow *parent, wxWindowID id,
                             const wxString &title, const wxPoint &pos,
                             const wxSize &size, long style, grib_pi *ppi,
                             double scale_factor)
    : GRIBUICtrlBarBase(parent, id, title, pos, size, style, scale_factor) {
  pParent = parent;
  pPlugIn = ppi;
  // Preinitialize the vierwport with an existing value, see
  // https://github.com/OpenCPN/OpenCPN/pull/4002/files
  m_vpMouse = new PlugIn_ViewPort(pPlugIn->GetCurrentViewPort());
  pReq_Dialog = nullptr;
  m_bGRIBActiveFile = nullptr;
  m_pTimelineSet = nullptr;
  m_gCursorData = nullptr;
  m_gGRIBUICData = nullptr;
  m_gtk_started = false;

  wxFileConfig *pConf = GetOCPNConfigObject();

  m_gGrabber = new GribGrabberWin(this);  // add the grabber to the dialog
  m_fgCtrlGrabberSize->Add(m_gGrabber, 0, wxALL, 0);

  this->SetSizer(m_fgCtrlBarSizer);
  this->Layout();
  m_fgCtrlBarSizer->Fit(this);

  // Initialize the time format check timer
  m_tFormatRefresh.Connect(
      wxEVT_TIMER, wxTimerEventHandler(GRIBUICtrlBar::OnFormatRefreshTimer),
      nullptr, this);

  // Sample the current time format using a fixed reference date (January 1,
  // 2021 at noon)
  wxDateTime referenceDate(1, wxDateTime::Jan, 2021, 12, 0, 0);
  m_sLastTimeFormat = toUsrDateTimeFormat_Plugin(referenceDate);

  // Start timer to check for time format changes (every 5 seconds)
  m_tFormatRefresh.Start(5000);

  if (pConf) {
    pConf->SetPath(_T ( "/Settings/GRIB" ));
    pConf->Read(_T ( "WindPlot" ), &m_bDataPlot[GribOverlaySettings::WIND],
                true);
    pConf->Read(_T ( "WindGustPlot" ),
                &m_bDataPlot[GribOverlaySettings::WIND_GUST], false);
    pConf->Read(_T ( "PressurePlot" ),
                &m_bDataPlot[GribOverlaySettings::PRESSURE], false);
    pConf->Read(_T ( "WavePlot" ), &m_bDataPlot[GribOverlaySettings::WAVE],
                false);
    pConf->Read(_T ( "CurrentPlot" ),
                &m_bDataPlot[GribOverlaySettings::CURRENT], false);
    pConf->Read(_T ( "PrecipitationPlot" ),
                &m_bDataPlot[GribOverlaySettings::PRECIPITATION], false);
    pConf->Read(_T ( "CloudPlot" ), &m_bDataPlot[GribOverlaySettings::CLOUD],
                false);
    pConf->Read(_T ( "AirTemperaturePlot" ),
                &m_bDataPlot[GribOverlaySettings::AIR_TEMPERATURE], false);
    pConf->Read(_T ( "SeaTemperaturePlot" ),
                &m_bDataPlot[GribOverlaySettings::SEA_TEMPERATURE], false);
    pConf->Read(_T ( "CAPEPlot" ), &m_bDataPlot[GribOverlaySettings::CAPE],
                false);
    pConf->Read(_T ( "CompReflectivityPlot" ),
                &m_bDataPlot[GribOverlaySettings::COMP_REFL], false);

    pConf->Read(_T ( "CursorDataShown" ), &m_CDataIsShown, true);

    pConf->Read(_T ( "lastdatatype" ), &m_lastdatatype, 0);

    pConf->SetPath(_T ( "/Settings/GRIB/FileNames" ));
    m_file_names.Clear();
    if (pConf->GetNumberOfEntries()) {
      wxString str, val;
      long dummy;
      bool bCont = pConf->GetFirstEntry(str, dummy);
      while (bCont) {
        pConf->Read(str, &val);  // Get a file name
        m_file_names.Add(val);
        bCont = pConf->GetNextEntry(str, dummy);
      }
    }

    wxStandardPathsBase &spath = wxStandardPaths::Get();

    pConf->SetPath(_T ( "/Directories" ));
    pConf->Read(_T ( "GRIBDirectory" ), &m_grib_dir);

    pConf->SetPath(_T( "/PlugIns/GRIB" ));
    pConf->Read(_T( "ManualRequestZoneSizing" ), &m_SavedZoneSelMode, 0);

    // Read XyGrib related configuration
    pConf->SetPath(_T ( "/Settings/GRIB/XyGrib" ));
    pConf->Read(_T( "AtmModelIndex" ), &xyGribConfig.atmModelIndex, 0);
    pConf->Read(_T( "WaveModelIndex" ), &xyGribConfig.waveModelIndex, 0);
    pConf->Read(_T( "ResolutionIndex" ), &xyGribConfig.resolutionIndex, 0);
    pConf->Read(_T( "DurationIndex" ), &xyGribConfig.durationIndex, 0);
    pConf->Read(_T( "RunIndex" ), &xyGribConfig.runIndex, 0);
    pConf->Read(_T( "IntervalIndex" ), &xyGribConfig.intervalIndex, 0);
    pConf->Read(_T( "Wind" ), &xyGribConfig.wind, true);
    pConf->Read(_T( "Gust" ), &xyGribConfig.gust, true);
    pConf->Read(_T( "Pressure" ), &xyGribConfig.pressure, false);
    pConf->Read(_T( "Temperature" ), &xyGribConfig.temperature, true);
    pConf->Read(_T( "Cape" ), &xyGribConfig.cape, false);
    pConf->Read(_T( "Reflectivity" ), &xyGribConfig.reflectivity, false);
    pConf->Read(_T( "CloudCover" ), &xyGribConfig.cloudCover, true);
    pConf->Read(_T( "Precipitation" ), &xyGribConfig.precipitation, true);
    pConf->Read(_T( "WaveHeight" ), &xyGribConfig.waveHeight, true);
    pConf->Read(_T( "WindWaves" ), &xyGribConfig.windWaves, true);
  }
  // init zone selection parameters
  m_ZoneSelMode = m_SavedZoneSelMode;

  // connect Timer
  m_tPlayStop.Connect(wxEVT_TIMER,
                      wxTimerEventHandler(GRIBUICtrlBar::OnPlayStopTimer),
                      nullptr, this);
  // connect functions
  Connect(wxEVT_MOVE, wxMoveEventHandler(GRIBUICtrlBar::OnMove));

  m_OverlaySettings.Read();

  DimeWindow(this);

  Fit();
  SetMinSize(GetBestSize());
  if (m_ProjectBoatPanel) {
    m_ProjectBoatPanel->SetSpeed(pPlugIn->m_boat_sog);
    m_ProjectBoatPanel->SetCourse(pPlugIn->m_boat_cog);
  }
  m_highlight_latmax = 0;
  m_highlight_lonmax = 0;
  m_highlight_latmin = 0;
  m_highlight_lonmin = 0;

#ifndef __OCPN__ANDROID__
  // Create the dialog for downloading GRIB files.
  // This dialog is created here, but not shown until the user requests it.
  // This is done such that user can draw bounding box on the chart before
  // the dialog is shown.

  createRequestDialog();
#endif
}

GRIBUICtrlBar::~GRIBUICtrlBar() {
  wxFileConfig *pConf = GetOCPNConfigObject();
  ;

  if (pConf) {
    pConf->SetPath(_T ( "/Settings/GRIB" ));
    pConf->Write(_T ( "WindPlot" ), m_bDataPlot[GribOverlaySettings::WIND]);
    pConf->Write(_T ( "WindGustPlot" ),
                 m_bDataPlot[GribOverlaySettings::WIND_GUST]);
    pConf->Write(_T ( "PressurePlot" ),
                 m_bDataPlot[GribOverlaySettings::PRESSURE]);
    pConf->Write(_T ( "WavePlot" ), m_bDataPlot[GribOverlaySettings::WAVE]);
    pConf->Write(_T ( "CurrentPlot" ),
                 m_bDataPlot[GribOverlaySettings::CURRENT]);
    pConf->Write(_T ( "PrecipitationPlot" ),
                 m_bDataPlot[GribOverlaySettings::PRECIPITATION]);
    pConf->Write(_T ( "CloudPlot" ), m_bDataPlot[GribOverlaySettings::CLOUD]);
    pConf->Write(_T ( "AirTemperaturePlot" ),
                 m_bDataPlot[GribOverlaySettings::AIR_TEMPERATURE]);
    pConf->Write(_T ( "SeaTemperaturePlot" ),
                 m_bDataPlot[GribOverlaySettings::SEA_TEMPERATURE]);
    pConf->Write(_T ( "CAPEPlot" ), m_bDataPlot[GribOverlaySettings::CAPE]);
    pConf->Write(_T ( "CompReflectivityPlot" ),
                 m_bDataPlot[GribOverlaySettings::COMP_REFL]);

    pConf->Write(_T ( "CursorDataShown" ), m_CDataIsShown);

    pConf->Write(_T ( "lastdatatype" ), m_lastdatatype);

    pConf->SetPath(_T ( "/Settings/GRIB/FileNames" ));
    int iFileMax = pConf->GetNumberOfEntries();
    if (iFileMax) {
      wxString key;
      long dummy;
      for (int i = 0; i < iFileMax; i++) {
        if (pConf->GetFirstEntry(key, dummy)) pConf->DeleteEntry(key, false);
      }
    }

    for (unsigned int i = 0; i < m_file_names.GetCount(); i++) {
      wxString key;
      key.Printf(_T("Filename%d"), i);
      pConf->Write(key, m_file_names[i]);
    }

    pConf->SetPath(_T ( "/Directories" ));
    pConf->Write(_T ( "GRIBDirectory" ), m_grib_dir);

    // Write current XyGrib panel configuration to configuration file
    pConf->SetPath(_T ( "/Settings/GRIB/XyGrib" ));
    pConf->Write(_T( "AtmModelIndex" ), xyGribConfig.atmModelIndex);
    pConf->Write(_T( "WaveModelIndex" ), xyGribConfig.waveModelIndex);
    pConf->Write(_T( "ResolutionIndex" ), xyGribConfig.resolutionIndex);
    pConf->Write(_T( "DurationIndex" ), xyGribConfig.durationIndex);
    pConf->Write(_T( "RunIndex" ), xyGribConfig.runIndex);
    pConf->Write(_T( "IntervalIndex" ), xyGribConfig.intervalIndex);
    pConf->Write(_T( "Wind" ), xyGribConfig.wind);
    pConf->Write(_T( "Gust" ), xyGribConfig.gust);
    pConf->Write(_T( "Pressure" ), xyGribConfig.pressure);
    pConf->Write(_T( "Temperature" ), xyGribConfig.temperature);
    pConf->Write(_T( "Cape" ), xyGribConfig.cape);
    pConf->Write(_T( "Reflectivity" ), xyGribConfig.reflectivity);
    pConf->Write(_T( "CloudCover" ), xyGribConfig.cloudCover);
    pConf->Write(_T( "Precipitation" ), xyGribConfig.precipitation);
    pConf->Write(_T( "WaveHeight" ), xyGribConfig.waveHeight);
    pConf->Write(_T( "WindWaves" ), xyGribConfig.windWaves);
  }
  delete m_vpMouse;
  delete m_pTimelineSet;
}

void GRIBUICtrlBar::SetScaledBitmap(double factor) {
  //  Round to the nearest "quarter", to avoid rendering artifacts
  m_ScaledFactor = wxRound(factor * 4.0) / 4.0;
  // set buttons bitmap
  m_bpPrev->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(prev), _T("prev"), m_ScaledFactor));
  m_bpNext->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(next), _T("next"), m_ScaledFactor));
  m_bpAltitude->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(altitude), _T("altitude"), m_ScaledFactor));
  m_bpNow->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(now), _T("now"), m_ScaledFactor));
  m_bpZoomToCenter->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(zoomto), _T("zoomto"), m_ScaledFactor));
  m_bpPlay->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(play), _T("play"), m_ScaledFactor));
  m_bpShowCursorData->SetBitmapLabel(GetScaledBitmap(
      wxBitmap(m_CDataIsShown ? curdata : ncurdata),
      m_CDataIsShown ? _T("curdata") : _T("ncurdata"), m_ScaledFactor));
  if (m_bpOpenFile)
    m_bpOpenFile->SetBitmapLabel(
        GetScaledBitmap(wxBitmap(openfile), _T("openfile"), m_ScaledFactor));
  m_bpSettings->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(setting), _T("setting"), m_ScaledFactor));

  SetRequestButtonBitmap(m_ZoneSelMode);

  // Careful here, this MinSize() sets the final width of the control bar,
  // overriding the width of the wxChoice above it.
#ifdef __OCPN__ANDROID__
  m_sTimeline->SetSize(wxSize(20 * m_ScaledFactor, -1));
  m_sTimeline->SetMinSize(wxSize(20 * m_ScaledFactor, -1));
#else
  m_sTimeline->SetSize(wxSize(90 * m_ScaledFactor, -1));
  m_sTimeline->SetMinSize(wxSize(90 * m_ScaledFactor, -1));
#endif
}

void GRIBUICtrlBar::SetRequestButtonBitmap(int type) {
  if (nullptr == m_bpRequest) return;
  m_bpRequest->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(request), _T("request"), m_ScaledFactor));
  m_bpRequest->SetToolTip(_("Start a download request"));
}

void GRIBUICtrlBar::OpenFile(bool newestFile) {
  m_bpPlay->SetBitmapLabel(
      GetScaledBitmap(wxBitmap(play), _T("play"), m_ScaledFactor));
  m_cRecordForecast->Clear();
  pPlugIn->GetGRIBOverlayFactory()->ClearParticles();
  m_Altitude = 0;
  m_FileIntervalIndex = m_OverlaySettings.m_SlicesPerUpdate;
  delete m_bGRIBActiveFile;
  delete m_pTimelineSet;
  m_pTimelineSet = nullptr;
  m_sTimeline->SetValue(0);
  m_TimeLineHours = 0;
  m_InterpolateMode = false;
  m_pNowMode = false;
  m_SelectionIsSaved = false;
  m_HasAltitude = false;

  // get more recent file in default directory if necessary
  wxFileName f;
  if (newestFile)
    m_file_names.Clear();  // file names list must be cleared if we expect only
                           // the newest file! otherwise newest file is added to
                           // the previously recorded, what we don't want
  if (m_file_names.IsEmpty()) {  // in any case were there is no filename
                                 // previously recorded, we must take the newest
    m_file_names = GetFilesInDirectory();
    newestFile = true;
  }

  m_bGRIBActiveFile = new GRIBFile(m_file_names, pPlugIn->GetCopyFirstCumRec(),
                                   pPlugIn->GetCopyMissWaveRec(), newestFile);

  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();
  wxString title;
  if (m_bGRIBActiveFile->IsOK()) {
    wxFileName fn(m_bGRIBActiveFile->GetFileNames()[0]);
    title = (_("File: "));
    title.Append(fn.GetFullName());
    if (rsa->GetCount() == 0) {  // valid but empty file
      delete m_bGRIBActiveFile;
      m_bGRIBActiveFile = nullptr;
      title.Prepend(_("Error! ")).Append(_(" contains no valid data!"));
    } else {
      PopulateComboDataList();
      title.append(" (" +
                   toUsrDateTimeFormat_Plugin(
                       wxDateTime(m_bGRIBActiveFile->GetRefDateTime())) +
                   ")");

      if (rsa->GetCount() > 1) {
        GribRecordSet &first = rsa->Item(0), &second = rsa->Item(1),
                      &last = rsa->Item(rsa->GetCount() - 1);

        // compute ntotal time span
        wxTimeSpan span = wxDateTime(last.m_Reference_Time) -
                          wxDateTime(first.m_Reference_Time);
        m_TimeLineHours = span.GetHours();

        // get file interval index and update intervale choice if necessary
        int halfintermin(wxTimeSpan(wxDateTime(second.m_Reference_Time) -
                                    wxDateTime(first.m_Reference_Time))
                             .GetMinutes() /
                         2);
        for (m_FileIntervalIndex = 0;; m_FileIntervalIndex++) {
          if (m_OverlaySettings.GetMinFromIndex(m_FileIntervalIndex) >
              halfintermin)
            break;
        }
        if (m_FileIntervalIndex > 0) m_FileIntervalIndex--;
        if (m_OverlaySettings.m_SlicesPerUpdate > m_FileIntervalIndex)
          m_OverlaySettings.m_SlicesPerUpdate = m_FileIntervalIndex;
      }
    }
  } else {
    delete m_bGRIBActiveFile;
    m_bGRIBActiveFile = nullptr;
    title = _("No valid GRIB file");
  }
  pPlugIn->GetGRIBOverlayFactory()->SetMessage(title);
  SetTitle(title);
  SetTimeLineMax(false);
  SetFactoryOptions();
  if (pPlugIn->GetStartOptions() &&
      m_TimeLineHours != 0)  // fix a crash for one date files
    ComputeBestForecastForNow();
  else
    TimelineChanged();

  // populate  altitude choice and show if necessary
  if (m_pTimelineSet && m_bGRIBActiveFile)
    for (int i = 1; i < 5; i++) {
      if (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WIND_VX + i) !=
              wxNOT_FOUND &&
          m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WIND_VY + i) !=
              wxNOT_FOUND)
        m_HasAltitude = true;
    }
  m_Altitude = 0;  // set altitude at std

  // enable buttons according with file contents to ovoid crashes
#ifdef __OCPN__ANDROID__
  m_bpSettings->Enable(true);
#else
  m_bpSettings->Enable(m_pTimelineSet != nullptr);
#endif
  m_bpZoomToCenter->Enable(m_pTimelineSet != nullptr);

  m_sTimeline->Enable(m_pTimelineSet != nullptr && m_TimeLineHours);
  m_bpPlay->Enable(m_pTimelineSet != nullptr && m_TimeLineHours);

  m_bpPrev->Enable(m_pTimelineSet != nullptr && m_TimeLineHours);
  m_bpNext->Enable(m_pTimelineSet != nullptr && m_TimeLineHours);
  m_bpNow->Enable(m_pTimelineSet != nullptr && m_TimeLineHours);

  SetCanvasContextMenuItemViz(pPlugIn->m_MenuItem, m_TimeLineHours != 0);

  //
  if (m_bGRIBActiveFile == nullptr) {
    // there's no data we can use in this file
    return;
  }
  //  Try to verify that there will be at least one parameter in the GRIB file
  //  that is enabled for display This will ensure that at least "some" data is
  //  displayed on file change, and so avoid user confusion of no data shown.
  //  This is especially important if cursor tracking of data is disabled.

  bool bconfigOK = false;
  if (m_bDataPlot[GribOverlaySettings::WIND] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WIND_VX) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::WIND_GUST] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WIND_GUST) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::PRESSURE] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_PRESSURE) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::WAVE] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WVDIR) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::WAVE] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_HTSIGW) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::CURRENT] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_SEACURRENT_VX) !=
       wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::PRECIPITATION] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_PRECIP_TOT) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::CLOUD] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_CLOUD_TOT) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::AIR_TEMPERATURE] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_AIR_TEMP) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::SEA_TEMPERATURE] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_SEA_TEMP) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::CAPE] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_CAPE) != wxNOT_FOUND))
    bconfigOK = true;
  if (m_bDataPlot[GribOverlaySettings::COMP_REFL] &&
      (m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_COMP_REFL) != wxNOT_FOUND))
    bconfigOK = true;

  //  If no parameter seems to be enabled by config, enable them all just to be
  //  sure something shows.
  if (!bconfigOK) {
    for (int i = 0; i < (int)GribOverlaySettings::GEO_ALTITUDE; i++) {
      if (InDataPlot(i)) {
        m_bDataPlot[i] = true;
      }
    }
  }
}

bool GRIBUICtrlBar::GetGribZoneLimits(GribTimelineRecordSet *timelineSet,
                                      double *latmin, double *latmax,
                                      double *lonmin, double *lonmax) {
  // calculate the largest overlay size
  GribRecord **pGR = timelineSet->m_GribRecordPtrArray;
  double ltmi = -GRIB_NOTDEF, ltma = GRIB_NOTDEF, lnmi = -GRIB_NOTDEF,
         lnma = GRIB_NOTDEF;
  for (unsigned int i = 0; i < Idx_COUNT; i++) {
    GribRecord *pGRA = pGR[i];
    if (!pGRA) continue;
    if (pGRA->getLatMin() < ltmi) ltmi = pGRA->getLatMin();
    if (pGRA->getLatMax() > ltma) ltma = pGRA->getLatMax();
    if (pGRA->getLonMin() < lnmi) lnmi = pGRA->getLonMin();
    if (pGRA->getLonMax() > lnma) lnma = pGRA->getLonMax();
  }
  if (ltmi == -GRIB_NOTDEF || lnmi == -GRIB_NOTDEF || ltma == GRIB_NOTDEF ||
      lnma == GRIB_NOTDEF)
    return false;

  if (latmin) *latmin = ltmi;
  if (latmax) *latmax = ltma;
  if (lonmin) *lonmin = lnmi;
  if (lonmax) *lonmax = lnma;
  return true;
}

class FileCollector : public wxDirTraverser {
public:
  FileCollector(wxArrayString &files, const wxRegEx &pattern)
      : m_files(files), m_pattern(pattern) {}
  virtual wxDirTraverseResult OnFile(const wxString &filename) {
    if (m_pattern.Matches(filename)) m_files.Add(filename);
    return wxDIR_CONTINUE;
  }
  virtual wxDirTraverseResult OnDir(const wxString &WXUNUSED(dirname)) {
    return wxDIR_IGNORE;
  }

private:
  wxArrayString &m_files;
  const wxRegEx &m_pattern;
};

wxArrayString GRIBUICtrlBar::GetFilesInDirectory() {
  wxArrayString file_array;
  if (!wxDir::Exists(m_grib_dir)) return file_array;

  //    Get an array of GRIB file names in the target directory, not descending
  //    into subdirs
  wxRegEx pattern(_T(".+\\.gri?b2?(\\.(bz2|gz))?$"),
                  wxRE_EXTENDED | wxRE_ICASE | wxRE_NOSUB);
  FileCollector collector(file_array, pattern);
  wxDir dir(m_grib_dir);
  dir.Traverse(collector);
  file_array.Sort(
      CompareFileStringTime);  // sort the files by File Modification Date
  return file_array;
}

void GRIBUICtrlBar::SetCursorLatLon(double lat, double lon) {
  m_cursor_lon = lon;
  m_cursor_lat = lat;

  if (m_vpMouse && ((lat > m_vpMouse->lat_min) && (lat < m_vpMouse->lat_max)) &&
      ((lon > m_vpMouse->lon_min) && (lon < m_vpMouse->lon_max)))
    UpdateTrackingControl();
}

void GRIBUICtrlBar::UpdateTrackingControl() {
  if (!m_CDataIsShown) return;

  if (m_DialogStyle >> 1 == SEPARATED) {
    if (m_gGRIBUICData) {
      if (!m_gGRIBUICData->m_gCursorData->m_tCursorTrackTimer.IsRunning())
        m_gGRIBUICData->m_gCursorData->m_tCursorTrackTimer.Start(
            50, wxTIMER_ONE_SHOT);
    }
  } else {
    if (m_gCursorData) {
      if (!m_gCursorData->m_tCursorTrackTimer.IsRunning())
        m_gCursorData->m_tCursorTrackTimer.Start(50, wxTIMER_ONE_SHOT);
    }
  }
}

void GRIBUICtrlBar::OnShowCursorData(wxCommandEvent &event) {
  m_CDataIsShown = !m_CDataIsShown;
  m_bpShowCursorData->SetBitmapLabel(GetScaledBitmap(
      wxBitmap(m_CDataIsShown ? curdata : ncurdata),
      m_CDataIsShown ? _T("curdata") : _T("ncurdata"), m_ScaledFactor));
  SetDialogsStyleSizePosition(true);
}

void GRIBUICtrlBar::SetDialogsStyleSizePosition(bool force_recompute) {
  /*Not all plateforms accept the dynamic window style changes.
  So these changes are applied only after exit from the plugin and re-opening
  it*/

  if (!force_recompute &&
      (m_old_DialogStyle == m_DialogStyle  // recompute only if necessary
       ||
       (m_old_DialogStyle >> 1 == ATTACHED && m_DialogStyle >> 1 == ATTACHED)))
    return;

  bool m_HasCaption = GetWindowStyleFlag() == (wxCAPTION | wxCLOSE_BOX |
                                               wxSYSTEM_MENU | wxTAB_TRAVERSAL);

  /* first hide grabber, detach cursordata and set ctrl/buttons visibility to
  have CtrlBar in his "alone" version altitude button visibility is a special
  case ( i == 0 ) */
  int state = (m_DialogStyle >> 1 == ATTACHED && m_CDataIsShown) ? 0 : 1;
  for (unsigned i = 0; i < m_OverlaySettings.m_iCtrlBarCtrlVisible[state].Len();
       i++) {
    bool vis = i > 0 ? true : m_HasAltitude ? true : false;
    if (FindWindow(i + ID_CTRLALTITUDE))
      FindWindow(i + ID_CTRLALTITUDE)
          ->Show(m_OverlaySettings.m_iCtrlBarCtrlVisible[state].GetChar(i) ==
                     _T('X') &&
                 vis);
  }
  // initiate tooltips
  m_bpShowCursorData->SetToolTip(m_CDataIsShown ? _("Hide data at cursor")
                                                : _("Show data at cursor"));
  m_bpPlay->SetToolTip(_("Start play back"));

  m_gGrabber->Hide();
  // then hide and detach cursor data window
  if (m_gCursorData) {
    m_gCursorData->Hide();
    m_fgCDataSizer->Detach(m_gCursorData);
  }

  SetMinSize(wxSize(0, 0));

  // then cancel eventually Cursor data dialog (to be re-created later if
  // necessary )
  if (m_gGRIBUICData) {
    m_gGRIBUICData->Destroy();
    m_gGRIBUICData = nullptr;
  }

  if ((m_DialogStyle >> 1 == SEPARATED || !m_CDataIsShown) &&
      !m_HasCaption) {   // Size and show grabber if necessary
    Fit();               // each time CtrlData dialog will be alone
    m_gGrabber->Size();  // or separated
    m_gGrabber->Show();
  }

  if (m_CDataIsShown) {
    if (m_DialogStyle >> 1 == ATTACHED) {  // dialogs attached
      // generate CursorData
      if (!m_gCursorData) m_gCursorData = new CursorData(this, *this);
      pPlugIn->SetDialogFont(m_gCursorData);
      m_gCursorData->PopulateTrackingControls(false);
      // attach CursorData to CtrlBar if necessary
      if (m_fgCDataSizer->GetItem(m_gCursorData) == nullptr)
        m_fgCDataSizer->Add(m_gCursorData, 0);
      m_gCursorData->Show();

    } else if (m_DialogStyle >> 1 == SEPARATED) {  // dialogs isolated
      // create cursor data dialog
      m_gGRIBUICData = new GRIBUICData(*this);
      m_gGRIBUICData->m_gCursorData->PopulateTrackingControls(
          m_DialogStyle == SEPARATED_VERTICAL);
      pPlugIn->SetDialogFont(m_gGRIBUICData->m_gCursorData);
      m_gGRIBUICData->Fit();
      m_gGRIBUICData->Update();
      m_gGRIBUICData->Show();
      pPlugIn->MoveDialog(m_gGRIBUICData, pPlugIn->GetCursorDataXY());
      m_gGRIBUICData->Layout();
      m_gGRIBUICData->Fit();
    }
  }
  Layout();
  Fit();
  wxSize sd = GetSize();
#ifdef __WXGTK__
  if (!m_gtk_started && m_HasCaption /*&& sd.y == GetSize().y*/) {
    sd.y += 30;
    m_gtk_started = true;
  }
#endif
  SetSize(wxSize(sd.x, sd.y));
  SetMinSize(wxSize(sd.x, sd.y));
#ifdef __OCPN__ANDROID__
  wxRect tbRect = GetMasterToolbarRect();
  // qDebug() << "TBR" << tbRect.x << tbRect.y << tbRect.width << tbRect.height
  // << pPlugIn->GetCtrlBarXY().x << pPlugIn->GetCtrlBarXY().y;

  if (1) {
    wxPoint pNew = pPlugIn->GetCtrlBarXY();
    pNew.x = tbRect.x + tbRect.width + 4;
    pNew.y = 0;  // tbRect.y;
    pPlugIn->SetCtrlBarXY(pNew);
    // qDebug() << "pNew" << pNew.x;

    int widthAvail =
        GetCanvasByIndex(0)->GetClientSize().x - (tbRect.x + tbRect.width);

    if (sd.x > widthAvail) {
      // qDebug() << "Too big" << widthAvail << sd.x;

      int target_char_width = (float)widthAvail / 28;
      wxScreenDC dc;
      bool bOK = false;
      int pointSize = 20;
      int width, height;
      wxFont *sFont;
      while (!bOK) {
        // qDebug() << "PointSize" << pointSize;
        sFont = FindOrCreateFont_PlugIn(pointSize, wxFONTFAMILY_DEFAULT,
                                        wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
                                        FALSE);
        dc.GetTextExtent(_T("W"), &width, &height, nullptr, nullptr, sFont);
        if (width <= target_char_width) bOK = true;
        pointSize--;
        if (pointSize <= 10) bOK = true;
      }

      m_cRecordForecast->SetFont(*sFont);

      Layout();
      Fit();
      Hide();
      SetSize(wxSize(widthAvail, sd.y));
      SetMinSize(wxSize(widthAvail, sd.y));
      Show();
    }
  }
  wxPoint pNow = pPlugIn->GetCtrlBarXY();
  pNow.y = 0;
  pPlugIn->SetCtrlBarXY(pNow);

#endif

  pPlugIn->MoveDialog(this, pPlugIn->GetCtrlBarXY());
  m_old_DialogStyle = m_DialogStyle;
}

void GRIBUICtrlBar::OnAltitude(wxCommandEvent &event) {
  if (!m_HasAltitude) return;

  wxMenu *amenu = new wxMenu();
  amenu->Connect(wxEVT_COMMAND_MENU_SELECTED,
                 wxMenuEventHandler(GRIBUICtrlBar::OnMenuEvent), nullptr, this);

  for (int i = 0; i < 5; i++) {
    if (((m_pTimelineSet &&
          m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WIND_VX + i) !=
              wxNOT_FOUND &&
          m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WIND_VY + i) !=
              wxNOT_FOUND)) ||
        i == 0) {
      MenuAppend(
          amenu, ID_CTRLALTITUDE + 1000 + i,

          m_OverlaySettings.GetAltitudeFromIndex(
              i, m_OverlaySettings.Settings[GribOverlaySettings::PRESSURE]
                     .m_Units),
          wxITEM_RADIO);
    }
  }

  amenu->Check(ID_CTRLALTITUDE + 1000 + m_Altitude, true);

  PopupMenu(amenu);

  delete amenu;
}

void GRIBUICtrlBar::OnMove(wxMoveEvent &event) {
  int w, h;
  GetScreenPosition(&w, &h);
  pPlugIn->SetCtrlBarXY(wxPoint(w, h));
}

void GRIBUICtrlBar::OnMenuEvent(wxMenuEvent &event) {
  int id = event.GetId();
  wxCommandEvent evt;
  evt.SetId(id);
  int alt = m_Altitude;
  switch (id) {
    // sub menu altitude data
    case ID_CTRLALTITUDE + 1000:
      m_Altitude = 0;
      break;
    case ID_CTRLALTITUDE + 1001:
      m_Altitude = 1;
      break;
    case ID_CTRLALTITUDE + 1002:
      m_Altitude = 2;
      break;
    case ID_CTRLALTITUDE + 1003:
      m_Altitude = 3;
      break;
    case ID_CTRLALTITUDE + 1004:
      m_Altitude = 4;
      break;
    //   end sub menu
    case ID_BTNNOW:
      OnNow(evt);
      break;
    case ID_BTNZOOMTC:
      OnZoomToCenterClick(evt);
      break;
    case ID_BTNSHOWCDATA:
      OnShowCursorData(evt);
      break;
    case ID_BTNPLAY:
      OnPlayStop(evt);
      break;
    case ID_BTNOPENFILE:
      OnOpenFile(evt);
      break;
    case ID_BTNSETTING:
      OnSettings(evt);
      break;
    case ID_BTNREQUEST:
      OnRequestForecastData(evt);
  }
  if (alt != m_Altitude) {
    SetDialogsStyleSizePosition(true);
    SetFactoryOptions();  // Reload the visibility options
  }
}

void GRIBUICtrlBar::MenuAppend(wxMenu *menu, int id, wxString label,
                               wxItemKind kind, wxBitmap bitmap,
                               wxMenu *submenu) {
  wxMenuItem *item = new wxMenuItem(menu, id, label, _T(""), kind);
  // add a submenu to this item if necessary
  if (submenu) {
    item->SetSubMenu(submenu);
  }

  /* Menu font do not work properly for MSW (wxWidgets 3.2.1)
  #ifdef __WXMSW__
    wxFont *qFont = OCPNGetFont(_("Menu"), 0);
    item->SetFont(*qFont);
  #endif
  */

#if defined(__WXMSW__) || defined(__WXGTK__)
  if (!bitmap.IsSameAs(wxNullBitmap)) item->SetBitmap(bitmap);
#endif

  menu->Append(item);
}

void GRIBUICtrlBar::OnMouseEvent(wxMouseEvent &event) {
  if (event.RightDown()) {
    // populate menu
    wxMenu *xmenu = new wxMenu();
    xmenu->Connect(wxEVT_COMMAND_MENU_SELECTED,
                   wxMenuEventHandler(GRIBUICtrlBar::OnMenuEvent), nullptr,
                   this);

    if (m_HasAltitude) {  // eventually populate altitude choice
      wxMenu *smenu = new wxMenu();
      smenu->Connect(wxEVT_COMMAND_MENU_SELECTED,
                     wxMenuEventHandler(GRIBUICtrlBar::OnMenuEvent), nullptr,
                     this);

      for (int i = 0; i < 5; i++) {
        if (((m_pTimelineSet &&
              m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WIND_VX + i) !=
                  wxNOT_FOUND &&
              m_bGRIBActiveFile->m_GribIdxArray.Index(Idx_WIND_VY + i) !=
                  wxNOT_FOUND)) ||
            i == 0) {
          MenuAppend(
              smenu, ID_CTRLALTITUDE + 1000 + i,
              m_OverlaySettings.GetAltitudeFromIndex(
                  i, m_OverlaySettings.Settings[GribOverlaySettings::PRESSURE]
                         .m_Units),
              wxITEM_RADIO);
        }
      }
      smenu->Check(ID_CTRLALTITUDE + 1000 + m_Altitude, true);
      MenuAppend(
          xmenu, wxID_ANY, _("Select geopotential altitude"), wxITEM_NORMAL,
          GetScaledBitmap(wxBitmap(altitude), _T("altitude"), m_ScaledFactor),
          smenu);
    }
    MenuAppend(xmenu, ID_BTNNOW, _("Now"), wxITEM_NORMAL,
               GetScaledBitmap(wxBitmap(now), _T("now"), m_ScaledFactor));
    MenuAppend(xmenu, ID_BTNZOOMTC, _("Zoom To Center"), wxITEM_NORMAL,
               GetScaledBitmap(wxBitmap(zoomto), _T("zoomto"), m_ScaledFactor));
    MenuAppend(
        xmenu, ID_BTNSHOWCDATA,
        m_CDataIsShown ? _("Hide data at cursor") : _("Show data at cursor"),
        wxITEM_NORMAL,
        GetScaledBitmap(wxBitmap(m_CDataIsShown ? curdata : ncurdata),
                        m_CDataIsShown ? _T("curdata") : _T("ncurdata"),
                        m_ScaledFactor));
    MenuAppend(
        xmenu, ID_BTNPLAY,
        m_tPlayStop.IsRunning() ? _("Stop play back") : _("Start play back"),
        wxITEM_NORMAL,
        GetScaledBitmap(wxBitmap(m_tPlayStop.IsRunning() ? stop : play),
                        m_tPlayStop.IsRunning() ? _T("stop") : _T("play"),
                        m_ScaledFactor));
    MenuAppend(
        xmenu, ID_BTNOPENFILE, _("Open a new file"), wxITEM_NORMAL,
        GetScaledBitmap(wxBitmap(openfile), _T("openfile"), m_ScaledFactor));
    MenuAppend(
        xmenu, ID_BTNSETTING, _("Settings"), wxITEM_NORMAL,
        GetScaledBitmap(wxBitmap(setting), _T("setting"), m_ScaledFactor));
    bool requeststate1 = m_ZoneSelMode == AUTO_SELECTION ||
                         m_ZoneSelMode == SAVED_SELECTION ||
                         m_ZoneSelMode == START_SELECTION;
    bool requeststate3 = m_ZoneSelMode == DRAW_SELECTION;
    MenuAppend(xmenu, ID_BTNREQUEST,
               requeststate1 ? _("Request forecast data")
               : requeststate3
                   ? _("Draw requested Area or Click here to stop request")
                   : _("Valid Area and Continue"),
               wxITEM_NORMAL,
               GetScaledBitmap(wxBitmap(requeststate1   ? request
                                        : requeststate3 ? selzone
                                                        : request_end),
                               requeststate1   ? _T("request")
                               : requeststate3 ? _T("selzone")
                                               : _T("request_end"),
                               m_ScaledFactor));

    PopupMenu(xmenu);

    delete xmenu;

    return;
  }

  if (m_DialogStyle >> 1 == SEPARATED) return;
  wxMouseEvent evt(event);
  evt.SetId(1000);

#ifndef __OCPN__ANDROID__
  if (m_gCursorData && m_CDataIsShown) {
    m_gCursorData->OnMouseEvent(evt);
  }
#endif
}

void GRIBUICtrlBar::ContextMenuItemCallback(int id) {
  // deactivate cursor data update during menu callback
  bool dataisshown = m_CDataIsShown;
  m_CDataIsShown = false;
  //
  wxFileConfig *pConf = GetOCPNConfigObject();

  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();
  GRIBTable *table = new GRIBTable(*this);

  table->InitGribTable(rsa, GetNearestIndex(GetNow(), 0));
  table->SetTableSizePosition(m_vpMouse->pix_width, m_vpMouse->pix_height);

  table->ShowModal();

  // re-activate cursor data
  m_CDataIsShown = dataisshown;
  delete table;
}

void GRIBUICtrlBar::SetViewPortUnderMouse(PlugIn_ViewPort *vp) {
  if (m_vpMouse == vp) return;

  delete m_vpMouse;
  m_vpMouse = new PlugIn_ViewPort(*vp);

  if (pReq_Dialog) pReq_Dialog->OnVpUnderMouseChange(vp);
}

void GRIBUICtrlBar::SetViewPortWithFocus(PlugIn_ViewPort *vp) {
  if (pReq_Dialog) pReq_Dialog->OnVpWithFocusChange(vp);
}

void GRIBUICtrlBar::OnClose(wxCloseEvent &event) {
  StopPlayBack();
  if (m_gGRIBUICData) m_gGRIBUICData->Hide();
  if (pReq_Dialog)
    if (m_ZoneSelMode > START_SELECTION) {
      pReq_Dialog->StopGraphicalZoneSelection();
      m_ZoneSelMode = START_SELECTION;
      // SetRequestButtonBitmap( m_ZoneSelMode );
    }
  pPlugIn->SendTimelineMessage(wxInvalidDateTime);

  pPlugIn->OnGribCtrlBarClose();
}

void GRIBUICtrlBar::OnSize(wxSizeEvent &event) {
  //    Record the dialog size
  wxSize p = event.GetSize();
  pPlugIn->SetCtrlBarSizeXY(p);

  event.Skip();
}

void GRIBUICtrlBar::OnPaint(wxPaintEvent &event) {
  wxWindowListNode *node = this->GetChildren().GetFirst();
  wxPaintDC dc(this);
  while (node) {
    wxWindow *win = node->GetData();
    if (dynamic_cast<wxBitmapButton *>(win))
      dc.DrawBitmap(dynamic_cast<wxBitmapButton *>(win)->GetBitmap(), 5, 5,
                    false);
    node = node->GetNext();
  }
}

void GRIBUICtrlBar::createRequestDialog() {
  ::wxBeginBusyCursor();

  delete pReq_Dialog;  // delete to be re-created

  pReq_Dialog = new GribRequestSetting(*this);
  pPlugIn->SetDialogFont(pReq_Dialog);
  pPlugIn->SetDialogFont(pReq_Dialog->m_sScrolledDialog);
  pReq_Dialog->OnVpUnderMouseChange(m_vpMouse);
  pReq_Dialog->SetRequestDialogSize();
  if (::wxIsBusy()) ::wxEndBusyCursor();
}

void GRIBUICtrlBar::OnRequestForecastData(wxCommandEvent &event) {
  if (m_tPlayStop.IsRunning())
    return;  // do nothing when play back is running !

  /*if there is one instance of the dialog already visible, do nothing*/
  if (pReq_Dialog && pReq_Dialog->IsShown()) return;

  /*create new request dialog*/
  createRequestDialog();
  // need to set a position at start
  int w;
  ::wxDisplaySize(&w, nullptr);
  pReq_Dialog->Move((w - pReq_Dialog->GetSize().GetX()) / 2, 30);
  pReq_Dialog->Show();

  SetRequestButtonBitmap(m_ZoneSelMode);  // set appopriate bitmap
}

void GRIBUICtrlBar::OnSettings(wxCommandEvent &event) {
  if (m_tPlayStop.IsRunning())
    return;  // do nothing when play back is running !

  ::wxBeginBusyCursor();

  GribOverlaySettings initSettings = m_OverlaySettings;
  GribSettingsDialog *dialog = new GribSettingsDialog(
      *this, m_OverlaySettings, m_lastdatatype, m_FileIntervalIndex);
  // set font
  pPlugIn->SetDialogFont(dialog);
  for (size_t i = 0; i < dialog->m_nSettingsBook->GetPageCount(); i++) {
    wxScrolledWindow *sc =
        ((wxScrolledWindow *)dialog->m_nSettingsBook->GetPage(i));
    pPlugIn->SetDialogFont(sc);
  }  // end set font

  dialog->m_nSettingsBook->ChangeSelection(dialog->GetPageIndex());
  dialog->SetSettingsDialogSize();
  // need to set a position at start
  int w;
  ::wxDisplaySize(&w, nullptr);
  dialog->Move((w - dialog->GetSize().GetX()) / 2, 30);
  // end set position

  ::wxEndBusyCursor();

  if (dialog->ShowModal() == wxID_OK) {
    dialog->WriteSettings();
    m_OverlaySettings.Write();
    if (m_OverlaySettings.Settings[GribOverlaySettings::WIND].m_Units !=
            initSettings.Settings[GribOverlaySettings::WIND].m_Units &&
        (m_OverlaySettings.Settings[GribOverlaySettings::WIND].m_Units ==
             GribOverlaySettings::BFS ||
         initSettings.Settings[GribOverlaySettings::WIND].m_Units ==
             GribOverlaySettings::BFS))
      m_old_DialogStyle =
          STARTING_STATE_STYLE;  // must recompute dialogs size if wind unit
                                 // have been changed
  } else {
    m_OverlaySettings = initSettings;
    m_DialogStyle = initSettings.m_iCtrlandDataStyle;
  }
  ::wxBeginBusyCursor();

  dialog->SaveLastPage();
  if (!m_OverlaySettings.m_bInterpolate)
    m_InterpolateMode = false;  // Interpolate could have been unchecked
  SetTimeLineMax(true);
  SetFactoryOptions();

  SetDialogsStyleSizePosition(true);
  delete dialog;

  event.Skip();
}

#ifdef __OCPN__ANDROID__
wxString callActivityMethod_ss(const char *method, wxString parm);
#endif

void GRIBUICtrlBar::OnCompositeDialog(wxCommandEvent &event) {
  //  Grab the current settings values
  GribOverlaySettings initSettings = m_OverlaySettings;
  initSettings.Read();

  wxString json;
  wxString json_begin = initSettings.SettingsToJSON(json);
  wxLogMessage(json_begin);

  //  Pick up the required options from the Request dialog
  //  and add them to the JSON object
  //  Really, this just means the current viewport coordinates.
  //  Everything else is stored in Android app preferences bundle.

  PlugIn_ViewPort current_vp = pPlugIn->GetCurrentViewPort();

  double lon_min = wxRound(current_vp.lon_min) - 1;
  double lon_max = wxRound(current_vp.lon_max) + 1;
  double lat_min = wxRound(current_vp.lat_min) - 1;
  double lat_max = wxRound(current_vp.lat_max) + 1;

  wxJSONValue v;
  wxJSONReader reader;
  int numErrors = reader.Parse(json_begin, &v);
  if (numErrors > 0) {
    return;
  }

  v[_T("latMin")] = lat_min;
  v[_T("latMax")] = lat_max;
  v[_T("lonMin")] = lon_min;
  v[_T("lonMax")] = lon_max;

  //  Clear the file name field, so that a retrieved or selected file name can
  //  be returned
  v[_T("grib_file")] = _T("");

  wxJSONWriter w;
  wxString json_final;
  w.Write(v, json_final);
  wxLogMessage(json_final);

#ifdef __OCPN__ANDROID__
  wxString ret = callActivityMethod_ss("doGRIBActivity", json_final);
  wxLogMessage(ret);
#endif

  event.Skip();
}

void GRIBUICtrlBar::OpenFileFromJSON(wxString json) {
  // construct the JSON root object
  wxJSONValue root;
  // construct a JSON parser
  wxJSONReader reader;

  int numErrors = reader.Parse(json, &root);
  if (numErrors > 0) {
    return;
  }

  wxString file = root[(_T("grib_file"))].AsString();

  if (file.Length() && wxFileExists(file)) {
    wxFileName fn(file);
    m_grib_dir = fn.GetPath();
    m_file_names.Clear();
    m_file_names.Add(file);
    OpenFile();
  }
}

void GRIBUICtrlBar::OnPlayStop(wxCommandEvent &event) {
  if (m_tPlayStop.IsRunning()) {
    StopPlayBack();
  } else {
    m_bpPlay->SetBitmapLabel(
        GetScaledBitmap(wxBitmap(stop), _T("stop"), m_ScaledFactor));
    m_bpPlay->SetToolTip(_("Stop play back"));
    m_tPlayStop.Start(3000 / m_OverlaySettings.m_UpdatesPerSecond,
                      wxTIMER_CONTINUOUS);
    m_InterpolateMode = m_OverlaySettings.m_bInterpolate;
  }
}

void GRIBUICtrlBar::OnPlayStopTimer(wxTimerEvent &event) {
  if (m_sTimeline->GetValue() >= m_sTimeline->GetMax()) {
    if (m_OverlaySettings.m_bLoopMode) {
      if (m_OverlaySettings.m_LoopStartPoint) {
        ComputeBestForecastForNow();
        if (m_sTimeline->GetValue() >= m_sTimeline->GetMax())
          StopPlayBack();  // will stop playback
        return;
      } else
        m_sTimeline->SetValue(0);
    } else {
      StopPlayBack();  // will stop playback
      return;
    }
  } else {
    int value = m_pNowMode ? m_OverlaySettings.m_bInterpolate
                                 ? GetNearestValue(GetNow(), 1)
                                 : GetNearestIndex(GetNow(), 2)
                           : m_sTimeline->GetValue();
    m_sTimeline->SetValue(value + 1);
  }

  m_pNowMode = false;
  if (!m_InterpolateMode)
    m_cRecordForecast->SetSelection(m_sTimeline->GetValue());
  TimelineChanged();
}

void GRIBUICtrlBar::StopPlayBack() {
  if (m_tPlayStop.IsRunning()) {
    m_tPlayStop.Stop();
    m_bpPlay->SetBitmapLabel(
        GetScaledBitmap(wxBitmap(play), _T("play"), m_ScaledFactor));
    m_bpPlay->SetToolTip(_("Start play back"));
  }
}

void GRIBUICtrlBar::TimelineChanged() {
  if (!m_bGRIBActiveFile || (m_bGRIBActiveFile && !m_bGRIBActiveFile->IsOK())) {
    pPlugIn->GetGRIBOverlayFactory()->SetGribTimelineRecordSet(nullptr);
    return;
  }

  RestaureSelectionString();  // eventually restaure the previousely saved time
                              // label

  wxDateTime time = TimelineTime();
  SetGribTimelineRecordSet(GetTimeLineRecordSet(time));

  if (!m_InterpolateMode) {
    /* get closest value to update timeline */
    ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();
    GribRecordSet &sel = rsa->Item(m_cRecordForecast->GetCurrentSelection());
    wxDateTime t = sel.m_Reference_Time;
    m_sTimeline->SetValue(m_OverlaySettings.m_bInterpolate
                              ? wxTimeSpan(t - MinTime()).GetMinutes() /
                                    m_OverlaySettings.GetMinFromIndex(
                                        m_OverlaySettings.m_SlicesPerUpdate)
                              : m_cRecordForecast->GetCurrentSelection());
  } else {
    m_cRecordForecast->SetSelection(GetNearestIndex(time, 2));
    SaveSelectionString();  // memorize index and label
    wxString formattedTime = toUsrDateTimeFormat_Plugin(time);
    m_cRecordForecast->SetString(m_Selection_index,
                                 formattedTime);  // replace it by the
                                                  // interpolated time label
    m_cRecordForecast->SetStringSelection(
        formattedTime);  // ensure it's visible in the box
  }

  UpdateTrackingControl();

  pPlugIn->SendTimelineMessage(time);
  RequestRefresh(GetGRIBCanvas());
}

void GRIBUICtrlBar::RestaureSelectionString() {
  if (!m_SelectionIsSaved) return;

  int sel = m_cRecordForecast->GetSelection();
  m_cRecordForecast->SetString(m_Selection_index, m_Selection_label);
  m_cRecordForecast->SetSelection(sel);
  m_SelectionIsSaved = false;
}

int GRIBUICtrlBar::GetNearestIndex(wxDateTime time, int model) {
  /* get closest index to update combo box */
  size_t i;
  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();

  wxDateTime itime, ip1time;
  for (i = 0; i < rsa->GetCount() - 1; i++) {
    itime = rsa->Item(i).m_Reference_Time;
    ip1time = rsa->Item(i + 1).m_Reference_Time;
    if (ip1time >= time) break;
  }
  if (!model) return (time - itime > (ip1time - time) * 3) ? i + 1 : i;

  return model == 1 ? time == ip1time ? i : i + 1 : time == ip1time ? i + 1 : i;
}

int GRIBUICtrlBar::GetNearestValue(wxDateTime time, int model) {
  /* get closest value to update Time line */
  if (m_TimeLineHours == 0) return 0;
  wxDateTime itime, ip1time;
  int stepmin =
      m_OverlaySettings.GetMinFromIndex(m_OverlaySettings.m_SlicesPerUpdate);
  wxTimeSpan span = time - MinTime();
  int t = span.GetMinutes() / stepmin;
  itime = MinTime() +
          wxTimeSpan(t * stepmin / 60, (t * stepmin) % 60);  // time at t
  ip1time = itime + wxTimeSpan(stepmin / 60, stepmin % 60);  // time at t+1

  if (model == 1) return time == ip1time ? t + 1 : t;

  return (time - itime > (ip1time - time) * 3) ? t + 1 : t;
}

wxDateTime GRIBUICtrlBar::GetNow() {
  wxDateTime now = wxDateTime::Now();
  now.GetSecond(0);

  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();

  // Verify if we are outside of the file time range
  now = (now > rsa->Item(rsa->GetCount() - 1).m_Reference_Time)
            ? rsa->Item(rsa->GetCount() - 1).m_Reference_Time
        : (now < rsa->Item(0).m_Reference_Time) ? rsa->Item(0).m_Reference_Time
                                                : now;
  return now;
}

wxDateTime GRIBUICtrlBar::TimelineTime() {
  if (m_InterpolateMode) {
    int tl = (m_TimeLineHours == 0) ? 0 : m_sTimeline->GetValue();
    int stepmin =
        m_OverlaySettings.GetMinFromIndex(m_OverlaySettings.m_SlicesPerUpdate);
    return MinTime() + wxTimeSpan(tl * stepmin / 60, (tl * stepmin) % 60);
  }

  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();
  unsigned int index = m_cRecordForecast->GetCurrentSelection() < 1
                           ? 0
                           : m_cRecordForecast->GetCurrentSelection();
  if (rsa && index < rsa->GetCount()) return rsa->Item(index).m_Reference_Time;

  return wxDateTime::Now();
}

wxDateTime GRIBUICtrlBar::MinTime() {
  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();
  if (rsa && rsa->GetCount()) {
    GribRecordSet &first = rsa->Item(0);
    return first.m_Reference_Time;
  }
  return wxDateTime::Now();
}

GribTimelineRecordSet *GRIBUICtrlBar::GetTimeLineRecordSet(wxDateTime time) {
  if (m_bGRIBActiveFile == nullptr) return nullptr;
  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();

  if (rsa->GetCount() == 0) return nullptr;

  GribTimelineRecordSet *set =
      new GribTimelineRecordSet(m_bGRIBActiveFile->GetCounter());
  for (int i = 0; i < Idx_COUNT; i++) {
    GribRecordSet *GRS1 = nullptr, *GRS2 = nullptr;
    GribRecord *GR1 = nullptr, *GR2 = nullptr;
    wxDateTime GR1time, GR2time;

    // already computed using polar interpolation from first axis
    if (set->m_GribRecordPtrArray[i]) continue;

    unsigned int j;
    for (j = 0; j < rsa->GetCount(); j++) {
      GribRecordSet *GRS = &rsa->Item(j);
      GribRecord *GR = GRS->m_GribRecordPtrArray[i];
      if (!GR) continue;

      wxDateTime curtime = GRS->m_Reference_Time;
      if (curtime <= time) GR1time = curtime, GRS1 = GRS, GR1 = GR;

      if (curtime >= time) {
        GR2time = curtime, GRS2 = GRS, GR2 = GR;
        break;
      }
    }

    if (!GR1 || !GR2) continue;

    wxDateTime mintime = MinTime();
    double minute2 = (GR2time - mintime).GetMinutes();
    double minute1 = (GR1time - mintime).GetMinutes();
    double nminute = (time - mintime).GetMinutes();

    if (minute2 < minute1 || nminute < minute1 || nminute > minute2) continue;

    double interp_const;
    if (minute1 == minute2) {
      // with big grib a copy is slow use a reference.
      set->m_GribRecordPtrArray[i] = GR1;
      continue;
    } else
      interp_const = (nminute - minute1) / (minute2 - minute1);

    /* if this is a vector interpolation use the 2d method */
    if (i < Idx_WIND_VY) {
      GribRecord *GR1y = GRS1->m_GribRecordPtrArray[i + Idx_WIND_VY];
      GribRecord *GR2y = GRS2->m_GribRecordPtrArray[i + Idx_WIND_VY];
      if (GR1y && GR2y) {
        GribRecord *Ry;
        set->SetUnRefGribRecord(
            i, GribRecord::Interpolated2DRecord(Ry, *GR1, *GR1y, *GR2, *GR2y,
                                                interp_const));
        set->SetUnRefGribRecord(i + Idx_WIND_VY, Ry);
        continue;
      }
    } else if (i <= Idx_WIND_VY300)
      continue;
    else if (i == Idx_SEACURRENT_VX) {
      GribRecord *GR1y = GRS1->m_GribRecordPtrArray[Idx_SEACURRENT_VY];
      GribRecord *GR2y = GRS2->m_GribRecordPtrArray[Idx_SEACURRENT_VY];
      if (GR1y && GR2y) {
        GribRecord *Ry;
        set->SetUnRefGribRecord(
            i, GribRecord::Interpolated2DRecord(Ry, *GR1, *GR1y, *GR2, *GR2y,
                                                interp_const));
        set->SetUnRefGribRecord(Idx_SEACURRENT_VY, Ry);
        continue;
      }
    } else if (i == Idx_SEACURRENT_VY)
      continue;

    set->SetUnRefGribRecord(i, GribRecord::InterpolatedRecord(
                                   *GR1, *GR2, interp_const, i == Idx_WVDIR));
  }

  set->m_Reference_Time = time.GetTicks();
  //(1-interp_const)*GRS1.m_Reference_Time + interp_const*GRS2.m_Reference_Time;
  return set;
}

void GRIBUICtrlBar::GetProjectedLatLon(int &x, int &y, PlugIn_ViewPort *vp) {
  wxPoint p(0, 0);
  auto now = TimelineTime();
  auto sog = m_ProjectBoatPanel->GetSpeed();
  auto cog = m_ProjectBoatPanel->GetCourse();
  double dist =
      static_cast<double>(now.GetTicks() - pPlugIn->m_boat_time) * sog / 3600.0;
  PositionBearingDistanceMercator_Plugin(pPlugIn->m_boat_lat,
                                         pPlugIn->m_boat_lon, cog, dist,
                                         &m_projected_lat, &m_projected_lon);
  if (vp) {
    GetCanvasPixLL(vp, &p, m_projected_lat, m_projected_lon);
  }
  x = p.x;
  y = p.y;
}

double GRIBUICtrlBar::getTimeInterpolatedValue(int idx, double lon, double lat,
                                               wxDateTime time) {
  if (m_bGRIBActiveFile == nullptr) return GRIB_NOTDEF;
  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();

  if (rsa->GetCount() == 0) return GRIB_NOTDEF;

  GribRecord *before = nullptr, *after = nullptr;

  unsigned int j;
  time_t t = time.GetTicks();
  for (j = 0; j < rsa->GetCount(); j++) {
    GribRecordSet *GRS = &rsa->Item(j);
    GribRecord *GR = GRS->m_GribRecordPtrArray[idx];
    if (!GR) continue;

    time_t curtime = GR->getRecordCurrentDate();
    if (curtime == t) return GR->getInterpolatedValue(lon, lat);

    if (curtime < t) before = GR;

    if (curtime > t) {
      after = GR;
      break;
    }
  }
  // time_t wxDateTime::GetTicks();
  if (!before || !after) return GRIB_NOTDEF;

  time_t t1 = before->getRecordCurrentDate();
  time_t t2 = after->getRecordCurrentDate();
  if (t1 == t2) return before->getInterpolatedValue(lon, lat);

  double v1 = before->getInterpolatedValue(lon, lat);
  double v2 = after->getInterpolatedValue(lon, lat);
  if (v1 != GRIB_NOTDEF && v2 != GRIB_NOTDEF) {
    double k = fabs((double)(t - t1) / (t2 - t1));
    return (1.0 - k) * v1 + k * v2;
  }

  return GRIB_NOTDEF;
}

bool GRIBUICtrlBar::getTimeInterpolatedValues(double &M, double &A, int idx1,
                                              int idx2, double lon, double lat,
                                              wxDateTime time) {
  M = GRIB_NOTDEF;
  A = GRIB_NOTDEF;

  if (m_bGRIBActiveFile == nullptr) return false;
  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();

  if (rsa->GetCount() == 0) return false;

  GribRecord *beforeX = nullptr, *afterX = nullptr;
  GribRecord *beforeY = nullptr, *afterY = nullptr;

  unsigned int j;
  time_t t = time.GetTicks();
  for (j = 0; j < rsa->GetCount(); j++) {
    GribRecordSet *GRS = &rsa->Item(j);
    GribRecord *GX = GRS->m_GribRecordPtrArray[idx1];
    GribRecord *GY = GRS->m_GribRecordPtrArray[idx2];
    if (!GX || !GY) continue;

    time_t curtime = GX->getRecordCurrentDate();
    if (curtime == t) {
      return GribRecord::getInterpolatedValues(M, A, GX, GY, lon, lat, true);
    }
    if (curtime < t) {
      beforeX = GX;
      beforeY = GY;
    }
    if (curtime > t) {
      afterX = GX;
      afterY = GY;
      break;
    }
  }
  // time_t wxDateTime::GetTicks();
  if (!beforeX || !afterX) return false;

  time_t t1 = beforeX->getRecordCurrentDate();
  time_t t2 = afterX->getRecordCurrentDate();
  if (t1 == t2) {
    return GribRecord::getInterpolatedValues(M, A, beforeX, beforeY, lon, lat,
                                             true);
  }
  double v1m, v2m, v1a, v2a;
  if (!GribRecord::getInterpolatedValues(v1m, v1a, beforeX, beforeY, lon, lat,
                                         true))
    return false;

  if (!GribRecord::getInterpolatedValues(v2m, v2a, afterX, afterY, lon, lat,
                                         true))
    return false;

  if (v1m == GRIB_NOTDEF || v2m == GRIB_NOTDEF || v1a == GRIB_NOTDEF ||
      v2a == GRIB_NOTDEF)
    return false;

  double k = fabs((double)(t - t1) / (t2 - t1));
  M = (1.0 - k) * v1m + k * v2m;
  A = (1.0 - k) * v1a + k * v2a;
  return true;
}

void GRIBUICtrlBar::OnTimeline(wxScrollEvent &event) {
  StopPlayBack();
  m_InterpolateMode = m_OverlaySettings.m_bInterpolate;
  if (!m_InterpolateMode)
    m_cRecordForecast->SetSelection(m_sTimeline->GetValue());
  m_pNowMode = false;
  TimelineChanged();
}

void GRIBUICtrlBar::OnOpenFile(wxCommandEvent &event) {
  if (m_tPlayStop.IsRunning())
    return;  // do nothing when play back is running !

#ifndef __OCPN__ANDROID__

  wxStandardPathsBase &path = wxStandardPaths::Get();
  wxString l_grib_dir = path.GetDocumentsDir();

  if (wxDir::Exists(m_grib_dir)) l_grib_dir = m_grib_dir;

  wxFileDialog *dialog =
      new wxFileDialog(nullptr, _("Select a GRIB file"), l_grib_dir, _T(""),
                       wxT("Grib files "
                           "(*.grb;*.bz2;*.gz;*.grib2;*.grb2)|*.grb;*.bz2;*.gz;"
                           "*.grib2;*.grb2|All files (*)|*.*"),
                       wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE,
                       wxDefaultPosition, wxDefaultSize, _T("File Dialog"));

  if (dialog->ShowModal() == wxID_OK) {
    ::wxBeginBusyCursor();

    m_grib_dir = dialog->GetDirectory();
    dialog->GetPaths(m_file_names);
    OpenFile();
    if (g_pi) {
      if (g_pi->m_bZoomToCenterAtInit) DoZoomToCenter();
    }
    SetDialogsStyleSizePosition(true);
  }
  delete dialog;
#else
  if (!wxDir::Exists(m_grib_dir)) {
    wxStandardPathsBase &path = wxStandardPaths::Get();
    m_grib_dir = path.GetDocumentsDir();
  }

  wxString file;
  int response = PlatformFileSelectorDialog(
      nullptr, &file, _("Select a GRIB file"), m_grib_dir, _T(""), _T("*.*"));

  if (response == wxID_OK) {
    wxFileName fn(file);
    m_grib_dir = fn.GetPath();
    m_file_names.Clear();
    m_file_names.Add(file);
    OpenFile();
    SetDialogsStyleSizePosition(true);
  }
#endif
}

void GRIBUICtrlBar::CreateActiveFileFromNames(const wxArrayString &filenames) {
  if (filenames.GetCount() != 0) {
    m_bGRIBActiveFile = nullptr;
    m_bGRIBActiveFile = new GRIBFile(filenames, pPlugIn->GetCopyFirstCumRec(),
                                     pPlugIn->GetCopyMissWaveRec());
  }
}

void GRIBUICtrlBar::PopulateComboDataList() {
  int index = 0;
  if (m_cRecordForecast->GetCount()) {
    index = m_cRecordForecast->GetCurrentSelection();
    m_cRecordForecast->Clear();
  }

  ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();
  for (size_t i = 0; i < rsa->GetCount(); i++) {
    wxDateTime t(rsa->Item(i).m_Reference_Time);
    m_cRecordForecast->Append(toUsrDateTimeFormat_Plugin(t));
  }
  m_cRecordForecast->SetSelection(index);
}

void GRIBUICtrlBar::OnZoomToCenterClick(wxCommandEvent &event) {
  DoZoomToCenter();
#if 0
    if(!m_pTimelineSet) return;

    double latmin,latmax,lonmin,lonmax;
    if(!GetGribZoneLimits(m_pTimelineSet, &latmin, &latmax, &lonmin, &lonmax ))
        return;

    //::wxBeginBusyCursor();

    //calculate overlay size
    double width = lonmax - lonmin;
    double height = latmax - latmin;

    // Calculate overlay center
    double clat = latmin + height / 2;
    double clon = lonmin + width / 2;

    //try to limit the ppm at a reasonable value
    if(width  > 120.){
        lonmin = clon - 60.;
        lonmax = clon + 60.;
    }
    if(height > 120.){
        latmin = clat - 60.;
        latmax = clat + 60.;
    }


    //Calculate overlay width & height in nm (around the center)
    double ow, oh;
    DistanceBearingMercator_Plugin(clat, lonmin, clat, lonmax, nullptr, &ow );
    DistanceBearingMercator_Plugin( latmin, clon, latmax, clon, nullptr, &oh );

    //calculate screen size
    int w = pPlugIn->GetGRIBOverlayFactory()->m_ParentSize.GetWidth();
    int h = pPlugIn->GetGRIBOverlayFactory()->m_ParentSize.GetHeight();

    //calculate final ppm scale to use
    double ppm;
    ppm = wxMin(w/(ow*1852), h/(oh*1852)) * ( 100 - fabs( clat ) ) / 90;

    ppm = wxMin(ppm, 1.0);

    JumpToPosition(clat, clon, ppm);

    RequestRefresh( pParent );
#endif
}

void GRIBUICtrlBar::DoZoomToCenter() {
  if (!m_pTimelineSet) return;

  double latmin, latmax, lonmin, lonmax;
  if (!GetGribZoneLimits(m_pTimelineSet, &latmin, &latmax, &lonmin, &lonmax))
    return;

  //::wxBeginBusyCursor();

  // calculate overlay size
  double width = lonmax - lonmin;
  double height = latmax - latmin;

  // Calculate overlay center
  double clat = latmin + height / 2;
  double clon = lonmin + width / 2;

  // try to limit the ppm at a reasonable value
  if (width > 120.) {
    lonmin = clon - 60.;
    lonmax = clon + 60.;
  }
  if (height > 120.) {
    latmin = clat - 60.;
    latmax = clat + 60.;
  }

  // Calculate overlay width & height in nm (around the center)
  double ow, oh;
  DistanceBearingMercator_Plugin(clat, lonmin, clat, lonmax, nullptr, &ow);
  DistanceBearingMercator_Plugin(latmin, clon, latmax, clon, nullptr, &oh);

  wxWindow *wx = GetGRIBCanvas();
  // calculate screen size
  int w = wx->GetSize().x;
  int h = wx->GetSize().y;

  // calculate final ppm scale to use
  double ppm;
  ppm = wxMin(w / (ow * 1852), h / (oh * 1852)) * (100 - fabs(clat)) / 90;

  ppm = wxMin(ppm, 1.0);

  CanvasJumpToPosition(wx, clat, clon, ppm);
}

void GRIBUICtrlBar::OnPrev(wxCommandEvent &event) {
  if (m_tPlayStop.IsRunning())
    return;  // do nothing when play back is running !

  RestaureSelectionString();

  int selection;
  if (m_pNowMode)
    selection = GetNearestIndex(GetNow(), 1);
  else if (m_InterpolateMode)
    selection =
        GetNearestIndex(TimelineTime(), 1); /* set to interpolated entry */
  else
    selection = m_cRecordForecast->GetCurrentSelection();

  m_pNowMode = false;
  m_InterpolateMode = false;

  m_cRecordForecast->SetSelection(selection < 1 ? 0 : selection - 1);

  TimelineChanged();
}

void GRIBUICtrlBar::OnNext(wxCommandEvent &event) {
  if (m_tPlayStop.IsRunning())
    return;  // do nothing when play back is running !

  RestaureSelectionString();

  int selection;
  if (m_pNowMode)
    selection = GetNearestIndex(GetNow(), 2);
  else if (m_InterpolateMode)
    selection =
        GetNearestIndex(TimelineTime(), 2); /* set to interpolated entry */
  else
    selection = m_cRecordForecast->GetCurrentSelection();

  m_cRecordForecast->SetSelection(selection);

  m_pNowMode = false;
  m_InterpolateMode = false;

  if (selection == (int)m_cRecordForecast->GetCount() - 1)
    return;  // end of list

  m_cRecordForecast->SetSelection(selection + 1);

  TimelineChanged();
}

void GRIBUICtrlBar::ComputeBestForecastForNow() {
  if (!m_bGRIBActiveFile || (m_bGRIBActiveFile && !m_bGRIBActiveFile->IsOK())) {
    pPlugIn->GetGRIBOverlayFactory()->SetGribTimelineRecordSet(nullptr);
    return;
  }

  wxDateTime now = GetNow();

  if (m_OverlaySettings.m_bInterpolate)
    m_sTimeline->SetValue(GetNearestValue(now, 0));
  else {
    m_cRecordForecast->SetSelection(GetNearestIndex(now, 0));
    m_sTimeline->SetValue(m_cRecordForecast->GetCurrentSelection());
  }

  if (pPlugIn->GetStartOptions() !=
      2) {  // no interpolation at start : take the nearest forecast
    m_InterpolateMode = m_OverlaySettings.m_bInterpolate;
    TimelineChanged();
    return;
  }
  // interpolation on 'now' at start
  m_InterpolateMode = true;
  m_pNowMode = true;
  SetGribTimelineRecordSet(
      GetTimeLineRecordSet(now));  // take current time & interpolate forecast

  RestaureSelectionString();  // eventually restaure the previousely saved
                              // wxChoice date time label
  m_cRecordForecast->SetSelection(GetNearestIndex(now, 2));
  SaveSelectionString();  // memorize the new selected wxChoice date time label
  wxString nowTime = toUsrDateTimeFormat_Plugin(now);
  m_cRecordForecast->SetString(m_Selection_index,
                               nowTime);  // write the now date time label
                                          // in the right place in wxChoice
  m_cRecordForecast->SetStringSelection(nowTime);  // put it in the box

  UpdateTrackingControl();

  pPlugIn->SendTimelineMessage(now);
  RequestRefresh(GetGRIBCanvas());
}

void GRIBUICtrlBar::SetGribTimelineRecordSet(
    GribTimelineRecordSet *pTimelineSet) {
  delete m_pTimelineSet;
  m_pTimelineSet = pTimelineSet;

  if (!pPlugIn->GetGRIBOverlayFactory()) return;

  pPlugIn->GetGRIBOverlayFactory()->SetGribTimelineRecordSet(m_pTimelineSet);
}

void GRIBUICtrlBar::SetTimeLineMax(bool SetValue) {
  int oldmax = wxMax(m_sTimeline->GetMax(), 1),
      oldval = m_sTimeline->GetValue();  // memorize the old range and value

  if (m_OverlaySettings.m_bInterpolate) {
    int stepmin =
        m_OverlaySettings.GetMinFromIndex(m_OverlaySettings.m_SlicesPerUpdate);
    m_sTimeline->SetMax(m_TimeLineHours * 60 / stepmin);
  } else {
    if (m_bGRIBActiveFile && m_bGRIBActiveFile->IsOK()) {
      ArrayOfGribRecordSets *rsa = m_bGRIBActiveFile->GetRecordSetArrayPtr();
      m_sTimeline->SetMax(rsa->GetCount() - 1);
    }
  }
  // try to retrieve a coherent timeline value with the new timeline range if it
  // has changed
  if (SetValue && m_sTimeline->GetMax() != 0) {
    if (m_pNowMode)
      ComputeBestForecastForNow();
    else
      m_sTimeline->SetValue(m_sTimeline->GetMax() * oldval / oldmax);
  }
}

void GRIBUICtrlBar::SetFactoryOptions() {
  if (m_pTimelineSet) m_pTimelineSet->ClearCachedData();

  pPlugIn->GetGRIBOverlayFactory()->ClearCachedData();

  UpdateTrackingControl();
  RequestRefresh(GetGRIBCanvas());
}

void GRIBUICtrlBar::OnFormatRefreshTimer(wxTimerEvent &event) {
  // Check if time format has changed by comparing current format with saved
  // format
  wxDateTime referenceDate(1, wxDateTime::Jan, 2021, 12, 0, 0);
  wxString currentFormat = toUsrDateTimeFormat_Plugin(referenceDate);

  if (currentFormat != m_sLastTimeFormat) {
    // Time format has changed, update all time displays
    m_sLastTimeFormat = currentFormat;

    if (m_bGRIBActiveFile && m_bGRIBActiveFile->IsOK()) {
      // Refresh the time format in the dropdown list
      PopulateComboDataList();

      // Update the timeline display
      TimelineChanged();

      // Request a refresh to update any on-screen displays
      RequestRefresh(GetGRIBCanvas());
    }
  }
}

//----------------------------------------------------------------------------------------------------------
//          GRIBFile Object Implementation
//----------------------------------------------------------------------------------------------------------
unsigned int GRIBFile::ID = 0;

GRIBFile::GRIBFile(const wxArrayString &file_names, bool CumRec, bool WaveRec,
                   bool newestFile)
    : m_counter(++ID) {
  m_bOK = false;  // Assume ok until proven otherwise
  m_pGribReader = nullptr;
  m_last_message = wxEmptyString;
  for (unsigned int i = 0; i < file_names.GetCount(); i++) {
    wxString file_name = file_names[i];
    if (::wxFileExists(file_name)) m_bOK = true;
  }

  if (m_bOK == false) {
    m_last_message = _(" files don't exist!");
    return;
  }
  //    Use the zyGrib support classes, as (slightly) modified locally....
  m_pGribReader = new GribReader();

  //    Read and ingest the entire GRIB file.......
  m_bOK = false;
  wxString file_name;
  for (unsigned int i = 0; i < file_names.GetCount(); i++) {
    file_name = file_names[i];
    m_pGribReader->openFile(file_name);

    if (m_pGribReader->isOk()) {
      m_bOK = true;
      if (newestFile) {
        break;
      }
    }
  }
  if (m_bOK == false) {
    m_last_message = _(" can't be read!");
    return;
  }

  if (newestFile) {
    m_FileNames.Clear();
    m_FileNames.Add(file_name);
  } else {
    m_FileNames = file_names;
  }

  // fixup Accumulation records
  m_pGribReader->computeAccumulationRecords(GRB_PRECIP_TOT, LV_GND_SURF, 0);
  m_pGribReader->computeAccumulationRecords(GRB_PRECIP_RATE, LV_GND_SURF, 0);
  m_pGribReader->computeAccumulationRecords(GRB_CLOUD_TOT, LV_ATMOS_ALL, 0);

  if (CumRec)
    m_pGribReader->copyFirstCumulativeRecord();  // add missing records if
                                                 // option selected
  if (WaveRec)
    m_pGribReader->copyMissingWaveRecords();  //  ""                   ""

  m_nGribRecords = m_pGribReader->getTotalNumberOfGribRecords();

  //    Walk the GribReader date list to populate our array of GribRecordSets

  std::set<time_t>::iterator iter;
  std::set<time_t> date_list = m_pGribReader->getListDates();
  for (iter = date_list.begin(); iter != date_list.end(); iter++) {
    GribRecordSet *t = new GribRecordSet(m_counter);
    time_t reftime = *iter;
    t->m_Reference_Time = reftime;
    m_GribRecordSetArray.Add(t);
  }

  //    Convert from zyGrib organization by data type/level to our organization
  //    by time.

  GribRecord *pRec;
  bool isOK(false);
  bool polarWind(false);
  bool polarCurrent(false);
  bool sigWave(false);
  bool sigH(false);
  //    Get the map of GribRecord vectors
  std::map<std::string, std::vector<GribRecord *> *> *p_map =
      m_pGribReader->getGribMap();

  //    Iterate over the map to get vectors of related GribRecords
  std::map<std::string, std::vector<GribRecord *> *>::iterator it;
  for (it = p_map->begin(); it != p_map->end(); it++) {
    std::vector<GribRecord *> *ls = (*it).second;
    for (zuint i = 0; i < ls->size(); i++) {
      pRec = ls->at(i);
      isOK = true;
      time_t thistime = pRec->getRecordCurrentDate();

      //   Search the GribRecordSet array for a GribRecordSet with matching time
      for (unsigned int j = 0; j < m_GribRecordSetArray.GetCount(); j++) {
        if (m_GribRecordSetArray.Item(j).m_Reference_Time == thistime) {
          int idx = -1, mdx = -1;
          switch (pRec->getDataType()) {
            case GRB_WIND_DIR:
              polarWind = true;
              // fall through
            case GRB_WIND_VX:
              if (pRec->getLevelType() == LV_ISOBARIC) {
                switch (pRec->getLevelValue()) {
                  case 300:
                    idx = Idx_WIND_VX300;
                    break;
                  case 500:
                    idx = Idx_WIND_VX500;
                    break;
                  case 700:
                    idx = Idx_WIND_VX700;
                    break;
                  case 850:
                    idx = Idx_WIND_VX850;
                    break;
                }
              } else
                idx = Idx_WIND_VX;
              break;
            case GRB_WIND_SPEED:
              polarWind = true;
              // fall through
            case GRB_WIND_VY:
              if (pRec->getLevelType() == LV_ISOBARIC) {
                switch (pRec->getLevelValue()) {
                  case 300:
                    idx = Idx_WIND_VY300;
                    break;
                  case 500:
                    idx = Idx_WIND_VY500;
                    break;
                  case 700:
                    idx = Idx_WIND_VY700;
                    break;
                  case 850:
                    idx = Idx_WIND_VY850;
                    break;
                }
              } else
                idx = Idx_WIND_VY;
              break;
            case GRB_CUR_DIR:
              polarCurrent = true;
              // fall through
            case GRB_UOGRD:
              idx = Idx_SEACURRENT_VX;
              break;
            case GRB_CUR_SPEED:
              polarCurrent = true;
              // fall through
            case GRB_VOGRD:
              idx = Idx_SEACURRENT_VY;
              break;
            case GRB_WIND_GUST:
              idx = Idx_WIND_GUST;
              break;
            case GRB_PRESSURE:
              idx = Idx_PRESSURE;
              break;
            case GRB_HTSGW:
              sigH = true;
              idx = Idx_HTSIGW;
              break;
            case GRB_PER:
              sigWave = true;
              idx = Idx_WVPER;
              break;
            case GRB_DIR:
              sigWave = true;
              idx = Idx_WVDIR;
              break;
            case GRB_WVHGT:
              idx = Idx_HTSIGW;
              break;  // Translation from NOAA WW3
            case GRB_WVPER:
              idx = Idx_WVPER;
              break;
            case GRB_WVDIR:
              idx = Idx_WVDIR;
              break;
            case GRB_PRECIP_RATE:
            case GRB_PRECIP_TOT:
              idx = Idx_PRECIP_TOT;
              break;
            case GRB_CLOUD_TOT:
              idx = Idx_CLOUD_TOT;
              break;
            case GRB_TEMP:
              if (pRec->getLevelType() == LV_ISOBARIC) {
                switch (pRec->getLevelValue()) {
                  case 300:
                    idx = Idx_AIR_TEMP300;
                    break;
                  case 500:
                    idx = Idx_AIR_TEMP500;
                    break;
                  case 700:
                    idx = Idx_AIR_TEMP700;
                    break;
                  case 850:
                    idx = Idx_AIR_TEMP850;
                    break;
                }
              } else
                idx = Idx_AIR_TEMP;
              if (pRec->getDataCenterModel() == NORWAY_METNO)
                mdx = 1000 + NORWAY_METNO;
              break;
            case GRB_WTMP:
              idx = Idx_SEA_TEMP;
              if (pRec->getDataCenterModel() == NOAA_GFS) mdx = 1000 + NOAA_GFS;
              break;
            case GRB_CAPE:
              idx = Idx_CAPE;
              break;
            case GRB_COMP_REFL:
              idx = Idx_COMP_REFL;
              break;
            case GRB_HUMID_REL:
              if (pRec->getLevelType() == LV_ISOBARIC) {
                switch (pRec->getLevelValue()) {
                  case 300:
                    idx = Idx_HUMID_RE300;
                    break;
                  case 500:
                    idx = Idx_HUMID_RE500;
                    break;
                  case 700:
                    idx = Idx_HUMID_RE700;
                    break;
                  case 850:
                    idx = Idx_HUMID_RE850;
                    break;
                }
              }
              break;
            case GRB_GEOPOT_HGT:
              if (pRec->getLevelType() == LV_ISOBARIC) {
                switch (pRec->getLevelValue()) {
                  case 300:
                    idx = Idx_GEOP_HGT300;
                    break;
                  case 500:
                    idx = Idx_GEOP_HGT500;
                    break;
                  case 700:
                    idx = Idx_GEOP_HGT700;
                    break;
                  case 850:
                    idx = Idx_GEOP_HGT850;
                    break;
                }
              }
              break;
          }
          if (idx == -1) {
            // XXX bug ?
            break;
          }

          bool skip = false;

          if (m_GribRecordSetArray.Item(j).m_GribRecordPtrArray[idx]) {
            // already one
            GribRecord *oRec =
                m_GribRecordSetArray.Item(j).m_GribRecordPtrArray[idx];
            if (idx == Idx_PRESSURE) {
              skip = (oRec->getLevelType() == LV_MSL);
            } else {
              // we favor UV over DIR/SPEED
              if (polarWind) {
                if (oRec->getDataType() == GRB_WIND_VY ||
                    oRec->getDataType() == GRB_WIND_VX)
                  skip = true;
              }
              if (polarCurrent) {
                if (oRec->getDataType() == GRB_UOGRD ||
                    oRec->getDataType() == GRB_VOGRD)
                  skip = true;
              }
              // favor average aka timeRange == 3 (HRRR subhourly subsets have
              // both 3 and 0 records for winds)
              if (!skip && (oRec->getTimeRange() == 3)) {
                skip = true;
              }
              // we favor significant Wave other wind wave.
              if (sigH) {
                if (oRec->getDataType() == GRB_HTSGW) skip = true;
              }
              if (sigWave) {
                if (oRec->getDataType() == GRB_DIR ||
                    oRec->getDataType() == GRB_PER)
                  skip = true;
              }
            }
          }
          if (!skip) {
            m_GribRecordSetArray.Item(j).m_GribRecordPtrArray[idx] = pRec;
            if (m_GribIdxArray.Index(idx) == wxNOT_FOUND)
              m_GribIdxArray.Add(idx, 1);
            if (mdx != -1 && m_GribIdxArray.Index(mdx) == wxNOT_FOUND)
              m_GribIdxArray.Add(mdx, 1);
          }
          break;
        }
      }
    }
  }

  if (polarWind || polarCurrent) {
    for (unsigned int j = 0; j < m_GribRecordSetArray.GetCount(); j++) {
      for (unsigned int i = 0; i < Idx_COUNT; i++) {
        int idx = -1;
        if (polarWind) {
          GribRecord *pRec =
              m_GribRecordSetArray.Item(j).m_GribRecordPtrArray[i];

          if (pRec != nullptr && pRec->getDataType() == GRB_WIND_DIR) {
            switch (i) {
              case Idx_WIND_VX300:
                idx = Idx_WIND_VY300;
                break;
              case Idx_WIND_VX500:
                idx = Idx_WIND_VY500;
                break;
              case Idx_WIND_VX700:
                idx = Idx_WIND_VY700;
                break;
              case Idx_WIND_VX850:
                idx = Idx_WIND_VY850;
                break;
              case Idx_WIND_VX:
                idx = Idx_WIND_VY;
                break;
              default:
                break;
            }
            if (idx != -1) {
              GribRecord *pRec1 =
                  m_GribRecordSetArray.Item(j).m_GribRecordPtrArray[idx];
              if (pRec1 != nullptr && pRec1->getDataType() == GRB_WIND_SPEED)
                GribRecord::Polar2UV(pRec, pRec1);
            }
          }
        }
        if (polarCurrent) {
          idx = -1;
          GribRecord *pRec =
              m_GribRecordSetArray.Item(j).m_GribRecordPtrArray[i];

          if (pRec != nullptr && pRec->getDataType() == GRB_CUR_DIR) {
            switch (i) {
              case Idx_SEACURRENT_VX:
                idx = Idx_SEACURRENT_VY;
                break;
              default:
                break;
            }
            if (idx != -1) {
              GribRecord *pRec1 =
                  m_GribRecordSetArray.Item(j).m_GribRecordPtrArray[idx];
              if (pRec1 != nullptr && pRec1->getDataType() == GRB_CUR_SPEED)
                GribRecord::Polar2UV(pRec, pRec1);
            }
          }
        }
      }
    }
  }

  if (isOK)
    m_pRefDateTime =
        pRec->getRecordRefDate();  // to ovoid crash with some bad files
}

GRIBFile::~GRIBFile() { delete m_pGribReader; }

//---------------------------------------------------------------------------------------
//               GRIB Cursor Data Ctrl & Display implementation
//---------------------------------------------------------------------------------------
GRIBUICData::GRIBUICData(GRIBUICtrlBar &parent)
#ifdef __WXOSX__
    : GRIBUICDataBase(parent.pParent, CURSOR_DATA, _("GRIB Display Control"),
                      wxDefaultPosition, wxDefaultSize,
                      wxSYSTEM_MENU | wxNO_BORDER | wxSTAY_ON_TOP)
#else
    : GRIBUICDataBase(&parent, CURSOR_DATA, _("GRIB Display Control"),
                      wxDefaultPosition, wxDefaultSize,
                      wxSYSTEM_MENU | wxNO_BORDER)
#endif
      ,
      m_gpparent(parent) {
  // m_gGrabber = new GribGrabberWin( this );
  //  fgSizer58->Add( m_gGrabber, 0, wxALL, 0 );

  m_gCursorData = new CursorData(this, m_gpparent);
  m_fgCdataSizer->Add(m_gCursorData, 0, wxALL, 0);

  Connect(wxEVT_MOVE, wxMoveEventHandler(GRIBUICData::OnMove));
}

void GRIBUICData::OnMove(wxMoveEvent &event) {
  int w, h;
  GetScreenPosition(&w, &h);
  m_gpparent.pPlugIn->SetCursorDataXY(wxPoint(w, h));
}

//---------------------------------------------------------------------------------------
//               Android Utility Methods
//---------------------------------------------------------------------------------------
#ifdef __OCPN__ANDROID__

#include <QtAndroidExtras/QAndroidJniObject>

bool CheckPendingJNIException() {
  if (!java_vm) {
    // qDebug() << "java_vm is nullptr.";
    return true;
  }

  JNIEnv *jenv;
  if (java_vm->GetEnv((void **)&jenv, JNI_VERSION_1_6) != JNI_OK) {
    // qDebug() << "GetEnv failed.";
    return true;
  }

  if ((jenv)->ExceptionCheck() == JNI_TRUE) {
    // qDebug() << "Found JNI Exception Pending.";
    return true;
  }

  return false;
}

wxString callActivityMethod_ss(const char *method, wxString parm) {
  if (!java_vm) {
    // qDebug() << "java_vm is nullptr.";
    return _T("NOK");
  }

  if (CheckPendingJNIException()) return _T("NOK");

  wxString return_string;
  QAndroidJniObject activity = QAndroidJniObject::callStaticObjectMethod(
      "org/qtproject/qt5/android/QtNative", "activity",
      "()Landroid/app/Activity;");
  if (CheckPendingJNIException()) return _T("NOK");

  if (!activity.isValid()) {
    // qDebug() << "Activity is not valid";
    return return_string;
  }

  //  Need a Java environment to decode the resulting string
  JNIEnv *jenv;
  if (java_vm->GetEnv((void **)&jenv, JNI_VERSION_1_6) != JNI_OK) {
    // qDebug() << "GetEnv failed.";
    return "jenv Error";
  }

  jstring p = (jenv)->NewStringUTF(parm.c_str());

  //  Call the desired method
  // qDebug() << "Calling method_ss";
  // qDebug() << method;

  QAndroidJniObject data = activity.callObjectMethod(
      method, "(Ljava/lang/String;)Ljava/lang/String;", p);
  if (CheckPendingJNIException()) return _T("NOK");

  // qDebug() << "Back from method_ss";

  jstring s = data.object<jstring>();

  if ((jenv)->GetStringLength(s)) {
    const char *ret_string = (jenv)->GetStringUTFChars(s, nullptr);
    return_string = wxString(ret_string, wxConvUTF8);
  }

  return return_string;
}

#endif
