/**************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  MarkProperties Support
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
 **************************************************************************/
#include "config.h"

// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/datetime.h>
#include <wx/clipbrd.h>
#include <wx/print.h>
#include <wx/printdlg.h>
#include <wx/stattext.h>
#include <wx/clrpicker.h>
#include <wx/bmpbuttn.h>

#include "chcanv.h"
#include "gui_lib.h"
#include "MarkInfo.h"
#include "model/georef.h"
#include "model/navutil_base.h"
#include "model/own_ship.h"
#include "model/position_parser.h"
#include "model/route.h"
#include "model/routeman.h"
#include "model/select.h"
#include "navutil.h"  // for Route
#include "ocpn_frame.h"
#include "OCPNPlatform.h"
#include "pluginmanager.h"
#include "routemanagerdialog.h"
#include "RoutePropDlgImpl.h"
#include "styles.h"
#include "svg_utils.h"
#include "TCWin.h"
#include "ui_utils.h"
#include "model/navobj_db.h"

#ifdef __ANDROID__
#include "androidUTIL.h"
#include <QtWidgets/QScroller>
#endif

extern TCMgr* ptcmgr;
extern MyConfig* pConfig;
extern Routeman* g_pRouteMan;
extern RouteManagerDialog* pRouteManagerDialog;
extern RoutePropDlgImpl* pRoutePropDialog;
extern ocpnStyle::StyleManager* g_StyleManager;

extern MyFrame* gFrame;
extern OCPNPlatform* g_Platform;
extern wxString g_default_wp_icon;

// Global print data, to remember settings during the session

// Global page setup data

extern float g_MarkScaleFactorExp;

extern MarkInfoDlg* g_pMarkInfoDialog;

WX_DECLARE_LIST(wxBitmap, BitmapList);
#include <wx/listimpl.cpp>
WX_DEFINE_LIST(BitmapList);

#include <wx/arrimpl.cpp>
WX_DEFINE_OBJARRAY(ArrayOfBitmaps);

#define EXTENDED_PROP_PAGE 2  // Index of the extended properties page

OCPNIconCombo::OCPNIconCombo(wxWindow* parent, wxWindowID id,
                             const wxString& value, const wxPoint& pos,
                             const wxSize& size, int n,
                             const wxString choices[], long style,
                             const wxValidator& validator, const wxString& name)
    : wxOwnerDrawnComboBox(parent, id, value, pos, size, n, choices, style,
                           validator, name) {
  double fontHeight =
      GetFont().GetPointSize() / g_Platform->getFontPointsperPixel();
  itemHeight = (int)wxRound(fontHeight);
}

OCPNIconCombo::~OCPNIconCombo() {}

void OCPNIconCombo::OnDrawItem(wxDC& dc, const wxRect& rect, int item,
                               int flags) const {
  int offset_x = bmpArray[item].GetWidth();
  int bmpHeight = bmpArray[item].GetHeight();
  dc.DrawBitmap(bmpArray[item], rect.x, rect.y + (rect.height - bmpHeight) / 2,
                true);

  if (flags & wxODCB_PAINTING_CONTROL) {
    wxString text = GetValue();
    int margin_x = 2;

#if wxCHECK_VERSION(2, 9, 0)
    if (ShouldUseHintText()) {
      text = GetHint();
      wxColour col = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
      dc.SetTextForeground(col);
    }

    margin_x = GetMargins().x;
#endif

    dc.DrawText(text, rect.x + margin_x + offset_x,
                (rect.height - dc.GetCharHeight()) / 2 + rect.y);
  } else {
    dc.DrawText(GetVListBoxComboPopup()->GetString(item), rect.x + 2 + offset_x,
                (rect.height - dc.GetCharHeight()) / 2 + rect.y);
  }
}

wxCoord OCPNIconCombo::OnMeasureItem(size_t item) const {
  int bmpHeight = bmpArray[item].GetHeight();

  return wxMax(itemHeight, bmpHeight);
}

wxCoord OCPNIconCombo::OnMeasureItemWidth(size_t item) const { return -1; }

int OCPNIconCombo::Append(const wxString& item, wxBitmap bmp) {
  bmpArray.Add(bmp);
  int idx = wxOwnerDrawnComboBox::Append(item);

  return idx;
}

void OCPNIconCombo::Clear(void) {
  wxOwnerDrawnComboBox::Clear();
  bmpArray.Clear();
}

//-------------------------------------------------------------------------------
//
//    Mark Properties Dialog Implementation
//
//-------------------------------------------------------------------------------
/*!
 * MarkProp type definition
 */

// DEFINE_EVENT_TYPE(EVT_LLCHANGE)           // events from LatLonTextCtrl
const wxEventType EVT_LLCHANGE = wxNewEventType();
//------------------------------------------------------------------------------
//    LatLonTextCtrl Window Implementation
//------------------------------------------------------------------------------
BEGIN_EVENT_TABLE(LatLonTextCtrl, wxWindow)
END_EVENT_TABLE()

// constructor
LatLonTextCtrl::LatLonTextCtrl(wxWindow* parent, wxWindowID id,
                               const wxString& value, const wxPoint& pos,
                               const wxSize& size, long style,
                               const wxValidator& validator,
                               const wxString& name)
    : wxTextCtrl(parent, id, value, pos, size, style, validator, name) {
  m_pParentEventHandler = parent->GetEventHandler();
}

void LatLonTextCtrl::OnKillFocus(wxFocusEvent& event) {
  //    Send an event to the Parent Dialog
  wxCommandEvent up_event(EVT_LLCHANGE, GetId());
  up_event.SetEventObject((wxObject*)this);
  m_pParentEventHandler->AddPendingEvent(up_event);
}

//-------------------------------------------------------------------------------
//
//    Mark Information Dialog Implementation
//
//-------------------------------------------------------------------------------
BEGIN_EVENT_TABLE(MarkInfoDlg, DIALOG_PARENT)
EVT_BUTTON(wxID_OK, MarkInfoDlg::OnMarkInfoOKClick)
EVT_BUTTON(wxID_CANCEL, MarkInfoDlg::OnMarkInfoCancelClick)
EVT_BUTTON(ID_BTN_DESC_BASIC, MarkInfoDlg::OnExtDescriptionClick)
EVT_BUTTON(ID_DEFAULT, MarkInfoDlg::DefautlBtnClicked)
EVT_BUTTON(ID_BTN_SHOW_TIDES, MarkInfoDlg::ShowTidesBtnClicked)
EVT_COMBOBOX(ID_BITMAPCOMBOCTRL, MarkInfoDlg::OnBitmapCombClick)
EVT_CHECKBOX(ID_CHECKBOX_SCAMIN_VIS, MarkInfoDlg::OnSelectScaMinExt)
EVT_TEXT(ID_DESCR_CTR_DESC, MarkInfoDlg::OnDescChangedExt)
EVT_TEXT(ID_DESCR_CTR_BASIC, MarkInfoDlg::OnDescChangedBasic)
EVT_TEXT(ID_LATCTRL, MarkInfoDlg::OnPositionCtlUpdated)
EVT_TEXT(ID_LONCTRL, MarkInfoDlg::OnPositionCtlUpdated)
EVT_CHOICE(ID_WPT_RANGERINGS_NO, MarkInfoDlg::OnWptRangeRingsNoChange)
// the HTML listbox's events
EVT_HTML_LINK_CLICKED(wxID_ANY, MarkInfoDlg::OnHtmlLinkClicked)
EVT_COMMAND(wxID_ANY, EVT_LAYOUT_RESIZE, MarkInfoDlg::OnLayoutResize)
EVT_CLOSE(MarkInfoDlg::OnClose)

// EVT_CHOICE( ID_WAYPOINTRANGERINGS, MarkInfoDef::OnWaypointRangeRingSelect )
END_EVENT_TABLE()

MarkInfoDlg::MarkInfoDlg(wxWindow* parent, wxWindowID id, const wxString& title,
                         const wxPoint& pos, const wxSize& size, long style) {
  DIALOG_PARENT::Create(parent, id, title, pos, size, style);

  wxFont* qFont = GetOCPNScaledFont(_("Dialog"));
  SetFont(*qFont);
  int metric = GetCharHeight();

#ifdef __ANDROID__
  //  Set Dialog Font by custom crafted Qt Stylesheet.
  wxString wqs = getFontQtStylesheet(qFont);
  wxCharBuffer sbuf = wqs.ToUTF8();
  QString qsb = QString(sbuf.data());
  QString qsbq = getQtStyleSheet();              // basic scrollbars, etc
  this->GetHandle()->setStyleSheet(qsb + qsbq);  // Concatenated style sheets
  wxScreenDC sdc;
  if (sdc.IsOk()) sdc.GetTextExtent(_T("W"), NULL, &metric, NULL, NULL, qFont);
#endif
  Create();
  m_pMyLinkList = NULL;
  SetColorScheme((ColorScheme)0);
  m_pRoutePoint = NULL;
  m_SaveDefaultDlg = NULL;
  CenterOnScreen();

#ifdef __WXOSX__
  Connect(wxEVT_ACTIVATE, wxActivateEventHandler(MarkInfoDlg::OnActivate), NULL,
          this);
#endif
}

void MarkInfoDlg::OnActivate(wxActivateEvent& event) {
  auto pWin = dynamic_cast<DIALOG_PARENT*>(event.GetEventObject());
  long int style = pWin->GetWindowStyle();
  if (event.GetActive())
    pWin->SetWindowStyle(style | wxSTAY_ON_TOP);
  else
    pWin->SetWindowStyle(style ^ wxSTAY_ON_TOP);
}

void MarkInfoDlg::initialize_images(void) {
  wxString iconDir = g_Platform->GetSharedDataDir() + _T("uidata/MUI_flat/");
  _img_MUI_settings_svg = LoadSVG(iconDir + _T("MUI_settings.svg"),
                                  2 * GetCharHeight(), 2 * GetCharHeight());

  ocpnStyle::Style* style = g_StyleManager->GetCurrentStyle();
  wxBitmap tide = style->GetIcon(_T("tidesml"));
  wxImage tide1 = tide.ConvertToImage();
  wxImage tide1s = tide1.Scale(m_sizeMetric * 3 / 2, m_sizeMetric * 3 / 2,
                               wxIMAGE_QUALITY_HIGH);
  m_bmTide = wxBitmap(tide1s);

  return;
}

void MarkInfoDlg::Create() {
  wxFont* qFont = GetOCPNScaledFont(_("Dialog"));
  SetFont(*qFont);
  m_sizeMetric = GetCharHeight();

#ifdef __ANDROID__
  //  Set Dialog Font by custom crafted Qt Stylesheet.

  wxString wqs = getFontQtStylesheet(qFont);
  wxCharBuffer sbuf = wqs.ToUTF8();
  QString qsb = QString(sbuf.data());

  QString qsbq = getAdjustedDialogStyleSheet();  // basic scrollbars, etc

  this->GetHandle()->setStyleSheet(qsb + qsbq);  // Concatenated style sheets

  wxScreenDC sdc;
  if (sdc.IsOk())
    sdc.GetTextExtent(_T("W"), NULL, &m_sizeMetric, NULL, NULL, qFont);

#endif

  initialize_images();

  wxBoxSizer* bSizer1;
  bSizer1 = new wxBoxSizer(wxVERTICAL);
  SetSizer(bSizer1);
  bSizer1->SetSizeHints(this);  // set size hints to honour minimum size

  // Notebook with fixed width tabs
  m_notebookProperties = new wxNotebook(this, wxID_ANY, wxDefaultPosition,
                                        wxDefaultSize, wxNB_FIXEDWIDTH);

  m_panelBasicProperties = new wxScrolledWindow(
      m_notebookProperties, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxHSCROLL | wxVSCROLL | wxTAB_TRAVERSAL);
#ifdef __ANDROID__
  m_panelBasicProperties->GetHandle()->setStyleSheet(
      getAdjustedDialogStyleSheet());
#endif

  // Basic panel
  m_panelBasicProperties->SetScrollRate(0, 2);
  m_notebookProperties->AddPage(m_panelBasicProperties, _("Basic"), true);
  bSizerBasicProperties = new wxBoxSizer(wxVERTICAL);
  m_panelBasicProperties->SetSizer(bSizerBasicProperties);

  // Layer notification
  m_staticTextLayer = new wxStaticText(
      m_panelBasicProperties, wxID_ANY,
      _("This waypoint is part of a layer and can't be edited"),
      wxDefaultPosition, wxDefaultSize, 0);
  m_staticTextLayer->Enable(false);
  bSizerBasicProperties->Add(m_staticTextLayer, 0, wxALL, 5);

  // Basic properties grid layout
  wxPanel* props_panel = new wxPanel(m_panelBasicProperties);
  FormGrid* props_sizer = new FormGrid(this);
  props_panel->SetSizer(props_sizer);
  bSizerBasicProperties->Add(props_panel, 0, wxALL | wxEXPAND, 16);
  int label_size = m_sizeMetric * 4;

  // Name property
  m_textName = new TextField(props_panel, _("Name"));

  // Show name checkbox
  wxStaticText* name_cb_label =
      new wxStaticText(props_panel, wxID_ANY, _("Show waypoint name"));
  m_checkBoxShowName =
      new wxCheckBox(props_panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxDefaultSize, wxALIGN_CENTER_VERTICAL);
  m_checkBoxShowName->Bind(wxEVT_CHECKBOX,
                           &MarkInfoDlg::OnShowWaypointNameSelectBasic, this);
  props_sizer->Add(name_cb_label, 0, wxALIGN_TOP);
  props_sizer->Add(m_checkBoxShowName, 0, wxEXPAND);

  // Icon property (with icon scaling)
  int icon_size = m_sizeMetric * 2;
  icon_size = wxMax(icon_size, (32 * g_MarkScaleFactorExp) + 4);
  m_bcomboBoxIcon =
      new OCPNIconCombo(props_panel, wxID_ANY, _("Combo!"), wxDefaultPosition,
                        wxDefaultSize, 0, NULL, wxCB_READONLY);
  m_bcomboBoxIcon->SetPopupMaxHeight(::wxGetDisplaySize().y / 2);
  m_bcomboBoxIcon->SetMinSize(wxSize(-1, icon_size));

  wxStaticText* icon_label = new wxStaticText(props_panel, wxID_ANY, _("Icon"));
  icon_label->SetMinSize(wxSize(label_size, -1));
  props_sizer->Add(icon_label, 0, wxALIGN_CENTER_VERTICAL);
  props_sizer->Add(m_bcomboBoxIcon, 0, wxEXPAND);

  // Lat/lon properties
  m_textLatitude = new TextField(props_panel, _("Latitude"));
  m_textLongitude = new TextField(props_panel, _("Longitude"));
  props_sizer->Fit(props_panel);

  // Description box
  wxStaticBox* desc_box =
      new wxStaticBox(m_panelBasicProperties, wxID_ANY, _("Description"));
  wxStaticBoxSizer* desc_sizer = new wxStaticBoxSizer(desc_box, wxHORIZONTAL);
  bSizerBasicProperties->Add(desc_sizer, 1, wxALL | wxEXPAND, 8);
  m_textDescription = new wxTextCtrl(
      m_panelBasicProperties, ID_DESCR_CTR_BASIC, wxEmptyString,
      wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
  m_textDescription->SetMinSize(wxSize(-1, 80));
  desc_sizer->Add(m_textDescription, 1, wxEXPAND);

  // Description expand button
  m_buttonExtDescription =
      new wxButton(m_panelBasicProperties, ID_BTN_DESC_BASIC, _T("..."),
                   wxDefaultPosition, wxSize(GetCharHeight() * 15 / 10, -1), 0);
  desc_sizer->Add(m_buttonExtDescription, 0, wxEXPAND);

  // Links box
  wxStaticBox* links_box =
      new wxStaticBox(m_panelBasicProperties, wxID_ANY, _("Links"));
  wxStaticBoxSizer* links_sizer = new wxStaticBoxSizer(links_box, wxHORIZONTAL);
  bSizerBasicProperties->Add(links_sizer, 1, wxALL | wxEXPAND, 8);

#ifndef __ANDROID__  // wxSimpleHtmlListBox is broken on Android....
  m_htmlList = new wxSimpleHtmlListBox(m_panelBasicProperties, wxID_ANY,
                                       wxDefaultPosition, wxDefaultSize, 0);
  links_sizer->Add(m_htmlList, 1, wxEXPAND);
#else

  m_scrolledWindowLinks =
      new wxScrolledWindow(m_panelBasicProperties, wxID_ANY, wxDefaultPosition,
                           wxSize(-1, 100), wxHSCROLL | wxVSCROLL);
  m_scrolledWindowLinks->SetMinSize(wxSize(-1, 80));
  m_scrolledWindowLinks->SetScrollRate(2, 2);
  links_sizer->Add(m_scrolledWindowLinks, 1, wxEXPAND);

  bSizerLinks = new wxBoxSizer(wxVERTICAL);
  m_scrolledWindowLinks->SetSizer(bSizerLinks);

  m_menuLink = new wxMenu();
  wxMenuItem* m_menuItemDelete;
  m_menuItemDelete = new wxMenuItem(m_menuLink, wxID_ANY, wxString(_("Delete")),
                                    wxEmptyString, wxITEM_NORMAL);
  m_menuLink->Append(m_menuItemDelete);

  wxMenuItem* m_menuItemEdit;
  m_menuItemEdit = new wxMenuItem(m_menuLink, wxID_ANY, wxString(_("Edit")),
                                  wxEmptyString, wxITEM_NORMAL);
  m_menuLink->Append(m_menuItemEdit);

  wxMenuItem* m_menuItemAdd;
  m_menuItemAdd = new wxMenuItem(m_menuLink, wxID_ANY, wxString(_("Add new")),
                                 wxEmptyString, wxITEM_NORMAL);
  m_menuLink->Append(m_menuItemAdd);

  wxBoxSizer* bSizer9 = new wxBoxSizer(wxHORIZONTAL);

  m_buttonAddLink =
      new wxButton(m_panelBasicProperties, wxID_ANY, _("Add new"),
                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  bSizer9->Add(m_buttonAddLink, 0, wxALL, 5);

  m_buttonAddLink->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
                           wxCommandEventHandler(MarkInfoDlg::OnAddLink), NULL,
                           this);

  links_sizer->Add(bSizer9, 0, wxEXPAND, 5);

#endif

  m_panelDescription =
      new wxPanel(m_notebookProperties, wxID_ANY, wxDefaultPosition,
                  wxDefaultSize, wxTAB_TRAVERSAL);
  wxBoxSizer* bSizer15;
  bSizer15 = new wxBoxSizer(wxVERTICAL);

  m_textCtrlExtDescription =
      new wxTextCtrl(m_panelDescription, ID_DESCR_CTR_DESC, wxEmptyString,
                     wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
  bSizer15->Add(m_textCtrlExtDescription, 1, wxALL | wxEXPAND, 5);

  m_panelDescription->SetSizer(bSizer15);
  m_notebookProperties->AddPage(m_panelDescription, _("Description"), false);

  /////////////////////////////////////////////////////// EXTENDED
  //////////////////////////////////////////////////////////

  m_panelExtendedProperties = new wxScrolledWindow(
      m_notebookProperties, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxHSCROLL | wxVSCROLL | wxTAB_TRAVERSAL);
#ifdef __ANDROID__
  m_panelExtendedProperties->GetHandle()->setStyleSheet(
      getAdjustedDialogStyleSheet());
#endif

  m_panelExtendedProperties->SetScrollRate(0, 2);

  wxBoxSizer* fSizerExtProperties = new wxBoxSizer(wxVERTICAL);
  m_panelExtendedProperties->SetSizer(fSizerExtProperties);
  m_notebookProperties->AddPage(m_panelExtendedProperties, _("Extended"),
                                false);

  sbSizerExtProperties = new wxStaticBoxSizer(
      wxVERTICAL, m_panelExtendedProperties, _("Extended Properties"));
  wxFlexGridSizer* gbSizerInnerExtProperties = new wxFlexGridSizer(3, 0, 0);
  gbSizerInnerExtProperties->AddGrowableCol(2);
  gbSizerInnerExtProperties->SetFlexibleDirection(wxBOTH);
  gbSizerInnerExtProperties->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

  m_checkBoxVisible = new wxCheckBox(sbSizerExtProperties->GetStaticBox(),
                                     ID_CHECKBOX_VIS_EXT, wxEmptyString);
  gbSizerInnerExtProperties->Add(m_checkBoxVisible);
  wxStaticText* m_staticTextVisible = new wxStaticText(
      sbSizerExtProperties->GetStaticBox(), wxID_ANY, _("Show on chart"));
  gbSizerInnerExtProperties->Add(m_staticTextVisible);
  gbSizerInnerExtProperties->Add(0, 0, 1, wxEXPAND, 0);

  m_checkBoxScaMin = new wxCheckBox(sbSizerExtProperties->GetStaticBox(),
                                    ID_CHECKBOX_SCAMIN_VIS, wxEmptyString);
  gbSizerInnerExtProperties->Add(m_checkBoxScaMin, 0, wxALIGN_CENTRE_VERTICAL,
                                 0);
  m_staticTextScaMin = new wxStaticText(sbSizerExtProperties->GetStaticBox(),
                                        wxID_ANY, _("Show at scale > 1 :"));
  gbSizerInnerExtProperties->Add(m_staticTextScaMin, 0, wxALIGN_CENTRE_VERTICAL,
                                 0);
  m_textScaMin = new wxTextCtrl(sbSizerExtProperties->GetStaticBox(), wxID_ANY);
  gbSizerInnerExtProperties->Add(m_textScaMin, 0, wxALL | wxEXPAND, 5);

  m_checkBoxShowNameExt = new wxCheckBox(sbSizerExtProperties->GetStaticBox(),
                                         wxID_ANY, wxEmptyString);
  m_checkBoxShowNameExt->Bind(wxEVT_CHECKBOX,
                              &MarkInfoDlg::OnShowWaypointNameSelectExt, this);
  gbSizerInnerExtProperties->Add(m_checkBoxShowNameExt);
  m_staticTextShowNameExt = new wxStaticText(
      sbSizerExtProperties->GetStaticBox(), wxID_ANY, _("Show waypoint name"));

  gbSizerInnerExtProperties->Add(m_staticTextShowNameExt);
  gbSizerInnerExtProperties->Add(0, 0, 1, wxEXPAND, 0);

  sbRangeRingsExtProperties = new wxStaticBoxSizer(
      wxVERTICAL, sbSizerExtProperties->GetStaticBox(), _("Range rings"));
  wxFlexGridSizer* gbRRExtProperties = new wxFlexGridSizer(4, 0, 0);
  gbRRExtProperties->AddGrowableCol(0);
  gbRRExtProperties->AddGrowableCol(1);
  gbRRExtProperties->AddGrowableCol(3);
  m_staticTextRR1 = new wxStaticText(sbSizerExtProperties->GetStaticBox(),
                                     wxID_ANY, _("Number"));
  gbRRExtProperties->Add(m_staticTextRR1, 0, wxLEFT, 5);
  m_staticTextRR2 = new wxStaticText(sbSizerExtProperties->GetStaticBox(),
                                     wxID_ANY, _("Distance"));
  gbRRExtProperties->Add(m_staticTextRR2, 0, wxLEFT, 5);
  gbRRExtProperties->Add(0, 0, 1, wxEXPAND, 5);  // a spacer
  m_staticTextRR4 = new wxStaticText(sbSizerExtProperties->GetStaticBox(),
                                     wxID_ANY, _("Color"));
  gbRRExtProperties->Add(m_staticTextRR4, 0, wxLEFT, 5);

  wxString rrAlt[] = {_("None"), _T( "1" ), _T( "2" ), _T( "3" ),
                      _T( "4" ), _T( "5" ), _T( "6" ), _T( "7" ),
                      _T( "8" ), _T( "9" ), _T( "10" )};
  m_ChoiceWaypointRangeRingsNumber =
      new wxChoice(sbSizerExtProperties->GetStaticBox(), ID_WPT_RANGERINGS_NO,
                   wxDefaultPosition, wxDefaultSize, 11, rrAlt);

  gbRRExtProperties->Add(m_ChoiceWaypointRangeRingsNumber, 0, wxALL | wxEXPAND,
                         5);
  m_textWaypointRangeRingsStep =
      new wxTextCtrl(sbSizerExtProperties->GetStaticBox(), wxID_ANY, _("0.05"),
                     wxDefaultPosition, wxDefaultSize, 0);
  gbRRExtProperties->Add(m_textWaypointRangeRingsStep, 0, wxALL | wxEXPAND, 5);

  wxString pDistUnitsStrings[] = {_("NMi"), _("km")};
  m_RangeRingUnits =
      new wxChoice(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                   wxDefaultPosition, wxDefaultSize, 2, pDistUnitsStrings);
  gbRRExtProperties->Add(m_RangeRingUnits, 0, wxALIGN_CENTRE_VERTICAL, 0);

  m_PickColor = new wxColourPickerCtrl(sbSizerExtProperties->GetStaticBox(),
                                       wxID_ANY, wxColour(0, 0, 0),
                                       wxDefaultPosition, wxDefaultSize, 0);
  gbRRExtProperties->Add(m_PickColor, 0, wxALL | wxEXPAND, 5);
  sbRangeRingsExtProperties->Add(
      gbRRExtProperties, 1,
      wxLEFT | wxTOP | wxEXPAND | wxALIGN_LEFT | wxALIGN_TOP, 5);

  sbSizerExtProperties->GetStaticBox()->Layout();

  wxFlexGridSizer* gbSizerInnerExtProperties2 = new wxFlexGridSizer(2, 0, 0);
  gbSizerInnerExtProperties2->AddGrowableCol(1);

  m_staticTextGuid =
      new wxStaticText(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                       _("GUID"), wxDefaultPosition, wxDefaultSize, 0);
  gbSizerInnerExtProperties2->Add(m_staticTextGuid, 0, wxALIGN_CENTRE_VERTICAL,
                                  0);
  m_textCtrlGuid = new wxTextCtrl(sbSizerExtProperties->GetStaticBox(),
                                  wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxDefaultSize, wxTE_READONLY);
  m_textCtrlGuid->SetEditable(false);
  gbSizerInnerExtProperties2->Add(m_textCtrlGuid, 0, wxALL | wxEXPAND, 5);

  wxFlexGridSizer* gbSizerInnerExtProperties1 = new wxFlexGridSizer(3, 0, 0);
  gbSizerInnerExtProperties1->AddGrowableCol(1);

  m_staticTextTideStation =
      new wxStaticText(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                       _("Tide Station"), wxDefaultPosition, wxDefaultSize, 0);
  gbSizerInnerExtProperties1->Add(m_staticTextTideStation, 0,
                                  wxALIGN_CENTRE_VERTICAL, 5);

#ifdef __ANDROID__
  m_choiceTideChoices.Add(_T(" "));
  m_comboBoxTideStation =
      new wxChoice(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                   wxDefaultPosition, wxDefaultSize, m_choiceTideChoices);

  gbSizerInnerExtProperties1->Add(
      m_comboBoxTideStation, 0, wxALL | wxEXPAND | wxALIGN_CENTRE_VERTICAL, 5);

#else
  m_comboBoxTideStation = new wxComboBox(
      sbSizerExtProperties->GetStaticBox(), wxID_ANY, wxEmptyString,
      wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_READONLY);
  gbSizerInnerExtProperties1->Add(
      m_comboBoxTideStation, 0, wxALL | wxEXPAND | wxALIGN_CENTRE_VERTICAL, 5);
#endif
  m_comboBoxTideStation->SetToolTip(
      _("Associate this waypoint with a tide station to quickly access tide "
        "predictions. Select from nearby stations or leave empty for no "
        "association."));

  m_buttonShowTides = new wxBitmapButton(
      sbSizerExtProperties->GetStaticBox(), ID_BTN_SHOW_TIDES, m_bmTide,
      wxDefaultPosition, m_bmTide.GetSize(), 0);
  gbSizerInnerExtProperties1->Add(m_buttonShowTides, 0,
                                  wxALL | wxALIGN_CENTRE_VERTICAL, 5);

  m_staticTextArrivalRadius = new wxStaticText(
      sbSizerExtProperties->GetStaticBox(), wxID_ANY, _("Arrival Radius"));
  gbSizerInnerExtProperties1->Add(m_staticTextArrivalRadius, 0,
                                  wxALIGN_CENTRE_VERTICAL, 0);
  m_textArrivalRadius =
      new wxTextCtrl(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                     wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
  m_textArrivalRadius->SetToolTip(
      _("Distance from the waypoint at which OpenCPN will consider the "
        "waypoint reached. Used for automatic waypoint advancement during "
        "active navigation."));
  gbSizerInnerExtProperties1->Add(m_textArrivalRadius, 0, wxALL | wxEXPAND, 5);
  m_staticTextArrivalUnits =
      new wxStaticText(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                       wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
  gbSizerInnerExtProperties1->Add(m_staticTextArrivalUnits, 0,
                                  wxALIGN_CENTRE_VERTICAL, 0);

  m_staticTextPlSpeed =
      new wxStaticText(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                       _("Planned Speed"), wxDefaultPosition, wxDefaultSize, 0);
  gbSizerInnerExtProperties1->Add(m_staticTextPlSpeed, 0,
                                  wxALIGN_CENTRE_VERTICAL, 0);
  m_textCtrlPlSpeed =
      new wxTextCtrl(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                     wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
  m_textCtrlPlSpeed->SetToolTip(_(
      "Enter the planned vessel speed for the leg FOLLOWING this waypoint. "
      "This speed is used when traveling FROM this waypoint TO the next "
      "waypoint in the route. The value is used to calculate estimated time "
      "of arrival at the next waypoint based on the ETD from this waypoint.\n\n"
      "If left blank, the route's default speed will be used for this leg. "
      "Individual waypoint speeds override the route-level speed setting."));
  gbSizerInnerExtProperties1->Add(m_textCtrlPlSpeed, 0, wxALL | wxEXPAND, 5);
  m_staticTextPlSpeedUnits =
      new wxStaticText(sbSizerExtProperties->GetStaticBox(), wxID_ANY,
                       getUsrSpeedUnit(), wxDefaultPosition, wxDefaultSize, 0);
  gbSizerInnerExtProperties1->Add(m_staticTextPlSpeedUnits, 0,
                                  wxALIGN_CENTRE_VERTICAL, 0);

  // The value of m_staticTextEtd is updated in UpdateProperties() based on
  // the date/time format specified in the "Options" dialog.
  m_staticTextEtd = new wxStaticText(sbSizerExtProperties->GetStaticBox(),
                                     wxID_ANY, _("ETD"));
  gbSizerInnerExtProperties1->Add(m_staticTextEtd, 0, wxALIGN_CENTRE_VERTICAL,
                                  0);
  wxBoxSizer* bsTimestamp = new wxBoxSizer(wxHORIZONTAL);
  m_cbEtdPresent = new wxCheckBox(sbSizerExtProperties->GetStaticBox(),
                                  wxID_ANY, wxEmptyString);
  m_cbEtdPresent->SetToolTip(
      _("Enable to manually set a planned departure time (ETD) for this "
        "waypoint.\n"
        "When checked, the specified date and time will be used instead of the "
        "automatically calculated ETD. This affects ETA calculations for "
        "subsequent waypoints in the route."));
  bsTimestamp->Add(m_cbEtdPresent, 0, wxALL | wxEXPAND, 5);
  m_EtdDatePickerCtrl = new wxDatePickerCtrl(
      sbSizerExtProperties->GetStaticBox(), ID_ETA_DATEPICKERCTRL,
      wxDefaultDateTime, wxDefaultPosition, wxDefaultSize, wxDP_DEFAULT,
      wxDefaultValidator);
  m_EtdDatePickerCtrl->SetToolTip(_(
      "Select the planned departure date (ETD) for this waypoint.\nUsed "
      "together with the time control to calculate arrival times at subsequent "
      "waypoints.\nETD information is only used for route planning "
      "and does not affect navigation."));
  bsTimestamp->Add(m_EtdDatePickerCtrl, 0, wxALL | wxEXPAND, 5);

#ifdef __WXGTK__
  m_EtdTimePickerCtrl =
      new TimeCtrl(sbSizerExtProperties->GetStaticBox(), ID_ETA_TIMEPICKERCTRL,
                   wxDefaultDateTime, wxDefaultPosition, wxDefaultSize);
#else
  m_EtdTimePickerCtrl = new wxTimePickerCtrl(
      sbSizerExtProperties->GetStaticBox(), ID_ETA_TIMEPICKERCTRL,
      wxDefaultDateTime, wxDefaultPosition, wxDefaultSize, wxDP_DEFAULT,
      wxDefaultValidator);
#endif

  bsTimestamp->Add(m_EtdTimePickerCtrl, 0, wxALL | wxEXPAND, 5);
  gbSizerInnerExtProperties1->Add(bsTimestamp, 0, wxEXPAND, 0);
  sbSizerExtProperties->Add(gbSizerInnerExtProperties, 0, wxALL | wxEXPAND, 5);
  sbSizerExtProperties->Add(sbRangeRingsExtProperties, 0, wxALL | wxEXPAND, 5);
  sbSizerExtProperties->Add(gbSizerInnerExtProperties2, 0, wxALL | wxEXPAND, 5);
  sbSizerExtProperties->Add(gbSizerInnerExtProperties1, 0, wxALL | wxEXPAND, 5);

  fSizerExtProperties->Add(sbSizerExtProperties, 1, wxALL | wxEXPAND);

  //-----------------
  bSizer1->Add(m_notebookProperties, 1, wxEXPAND);

  wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
  bSizer1->Add(btnSizer, 0, wxEXPAND, 0);

  DefaultsBtn =
      new wxBitmapButton(this, ID_DEFAULT, _img_MUI_settings_svg,
                         wxDefaultPosition, _img_MUI_settings_svg.GetSize(), 0);
  btnSizer->Add(DefaultsBtn, 0, wxALL | wxALIGN_LEFT | wxALIGN_BOTTOM, 5);
  btnSizer->Add(0, 0, 1, wxEXPAND);  // spacer

  m_sdbSizerButtons = new wxStdDialogButtonSizer();
  m_buttonOkay = new wxButton(this, wxID_OK);
  m_sdbSizerButtons->AddButton(m_buttonOkay);
  m_sdbSizerButtons->AddButton(new wxButton(this, wxID_CANCEL, _("Cancel")));
  m_sdbSizerButtons->Realize();
  btnSizer->Add(m_sdbSizerButtons, 0, wxALL, 5);

  // SetMinSize(wxSize(-1, 600));

  // Connect Events
  m_textLatitude->Connect(
      wxEVT_CONTEXT_MENU,
      wxCommandEventHandler(MarkInfoDlg::OnRightClickLatLon), NULL, this);
  m_textLongitude->Connect(
      wxEVT_CONTEXT_MENU,
      wxCommandEventHandler(MarkInfoDlg::OnRightClickLatLon), NULL, this);
#ifndef __ANDROID__  // wxSimpleHtmlListBox is broken on Android....
  m_htmlList->Connect(wxEVT_RIGHT_DOWN,
                      wxMouseEventHandler(MarkInfoDlg::m_htmlListContextMenu),
                      NULL, this);
#else
#endif
  m_notebookProperties->Connect(
      wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED,
      wxNotebookEventHandler(MarkInfoDlg::OnNotebookPageChanged), NULL, this);
  // m_EtdTimePickerCtrl->Connect( wxEVT_TIME_CHANGED, wxDateEventHandler(
  // MarkInfoDlg::OnTimeChanged ), NULL, this ); m_EtdDatePickerCtrl->Connect(
  // wxEVT_DATE_CHANGED, wxDateEventHandler( MarkInfoDlg::OnTimeChanged ), NULL,
  // this );
  m_comboBoxTideStation->Connect(
      wxEVT_COMMAND_COMBOBOX_SELECTED,
      wxCommandEventHandler(MarkInfoDlg::OnTideStationCombobox), NULL, this);
}

void MarkInfoDlg::OnClose(wxCloseEvent& event) {
  Hide();
  event.Veto();
  if (m_pRoutePoint) m_pRoutePoint->m_bRPIsBeingEdited = false;
}

#define TIDESTATION_BATCH_SIZE 10

void MarkInfoDlg::OnTideStationCombobox(wxCommandEvent& event) {
  int count = m_comboBoxTideStation->GetCount();
  int sel = m_comboBoxTideStation->GetSelection();
  if (sel == count - 1) {
    wxString n;
    int i = 0;
    for (auto ts : m_tss) {
      if (i == count + TIDESTATION_BATCH_SIZE) {
        break;
      }
      if (i > count) {
        n = wxString::FromUTF8(ts.second->IDX_station_name);
        m_comboBoxTideStation->Append(n);
      }
      i++;
    }
  }
}

void MarkInfoDlg::OnNotebookPageChanged(wxNotebookEvent& event) {
  if (event.GetSelection() == EXTENDED_PROP_PAGE) {
    if (m_lasttspos.IsSameAs(m_textLatitude->GetValue() +
                             m_textLongitude->GetValue())) {
      return;
    }
    m_lasttspos = m_textLatitude->GetValue() + m_textLongitude->GetValue();
    double lat = fromDMM(m_textLatitude->GetValue());
    double lon = fromDMM(m_textLongitude->GetValue());
    m_tss = ptcmgr->GetStationsForLL(lat, lon);
    wxString s = m_comboBoxTideStation->GetStringSelection();
    wxString n;
    int i = 0;
    m_comboBoxTideStation->Clear();
    m_comboBoxTideStation->Append(wxEmptyString);
    for (auto ts : m_tss) {
      if (i == TIDESTATION_BATCH_SIZE) {
        break;
      }
      i++;
      n = wxString::FromUTF8(ts.second->IDX_station_name);
      m_comboBoxTideStation->Append(n);
      if (s == n) {
        m_comboBoxTideStation->SetSelection(i);
      }
    }
    if (m_comboBoxTideStation->GetStringSelection() != s) {
      m_comboBoxTideStation->Insert(s, 1);
      m_comboBoxTideStation->SetSelection(1);
    }
  }
}

void MarkInfoDlg::RecalculateSize(void) {
#ifdef __ANDROID__

  Layout();

  wxSize dsize = GetParent()->GetClientSize();

  wxSize esize;

  esize.x = GetCharHeight() * 20;
  esize.y = GetCharHeight() * 40;
  // qDebug() << "esizeA" << esize.x << esize.y;

  esize.y = wxMin(esize.y, dsize.y - (2 * GetCharHeight()));
  esize.x = wxMin(esize.x, dsize.x - (1 * GetCharHeight()));
  SetSize(wxSize(esize.x, esize.y));
  // qDebug() << "esize" << esize.x << esize.y;

  wxSize fsize = GetSize();
  fsize.y = wxMin(fsize.y, dsize.y - (2 * GetCharHeight()));
  fsize.x = wxMin(fsize.x, dsize.x - (1 * GetCharHeight()));
  // qDebug() << "fsize" << fsize.x << fsize.y;

  //  And finally, not too tall...
  fsize.y = wxMin(fsize.y, (25 * GetCharHeight()));

  SetSize(wxSize(-1, fsize.y));

  m_defaultClientSize = GetClientSize();
  Center();
#else
  wxSize dsize = GetParent()->GetClientSize();
  SetSize(-1, wxMax(GetSize().y, dsize.y / 1.5));
#endif
}

MarkInfoDlg::~MarkInfoDlg() {
  // Disconnect Events
  m_textLatitude->Disconnect(
      wxEVT_CONTEXT_MENU,
      wxCommandEventHandler(MarkInfoDlg::OnRightClickLatLon), NULL, this);
  m_textLongitude->Disconnect(
      wxEVT_CONTEXT_MENU,
      wxCommandEventHandler(MarkInfoDlg::OnRightClickLatLon), NULL, this);
#ifndef __ANDROID__  // wxSimpleHtmlListBox is broken on Android....
  m_htmlList->Disconnect(
      wxEVT_RIGHT_DOWN, wxMouseEventHandler(MarkInfoDlg::m_htmlListContextMenu),
      NULL, this);
#else
#endif

  m_notebookProperties->Disconnect(
      wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED,
      wxNotebookEventHandler(MarkInfoDlg::OnNotebookPageChanged), NULL, this);
  m_EtdTimePickerCtrl->Disconnect(
      wxEVT_TIME_CHANGED, wxDateEventHandler(MarkInfoDlg::OnTimeChanged), NULL,
      this);
  m_EtdDatePickerCtrl->Disconnect(
      wxEVT_DATE_CHANGED, wxDateEventHandler(MarkInfoDlg::OnTimeChanged), NULL,
      this);

#ifdef __ANDROID__
  androidEnableBackButton(true);
#endif
}

void MarkInfoDlg::InitialFocus(void) {
  m_textName->SetFocus();
  m_textName->SetInsertionPointEnd();
}

void MarkInfoDlg::SetColorScheme(ColorScheme cs) { DimeControl(this); }

void MarkInfoDlg::ClearData() {
  m_pRoutePoint = NULL;
  UpdateProperties();
}

void MarkInfoDlg::SetRoutePoint(RoutePoint* pRP) {
  m_pRoutePoint = pRP;
  if (m_pRoutePoint) {
    m_lat_save = m_pRoutePoint->m_lat;
    m_lon_save = m_pRoutePoint->m_lon;
    m_IconName_save = m_pRoutePoint->GetIconName();
    m_bShowName_save = m_pRoutePoint->m_bShowName;
    m_bIsVisible_save = m_pRoutePoint->m_bIsVisible;
    m_Name_save = m_pRoutePoint->GetName();
    m_Description_save = m_pRoutePoint->m_MarkDescription;
    m_bUseScaMin_save = m_pRoutePoint->GetUseSca();
    m_iScaminVal_save = m_pRoutePoint->GetScaMin();

    if (m_pMyLinkList) delete m_pMyLinkList;
    m_pMyLinkList = new HyperlinkList();
    int NbrOfLinks = m_pRoutePoint->m_HyperlinkList->GetCount();
    if (NbrOfLinks > 0) {
      wxHyperlinkListNode* linknode =
          m_pRoutePoint->m_HyperlinkList->GetFirst();
      while (linknode) {
        Hyperlink* link = linknode->GetData();

        Hyperlink* h = new Hyperlink();
        h->DescrText = link->DescrText;
        h->Link = link->Link;
        h->LType = link->LType;

        m_pMyLinkList->Append(h);

        linknode = linknode->GetNext();
      }
    }
  }
}

void MarkInfoDlg::UpdateHtmlList() {
#ifndef __ANDROID__  // wxSimpleHtmlListBox is broken on Android....
  GetSimpleBox()->Clear();
  int NbrOfLinks = m_pRoutePoint->m_HyperlinkList->GetCount();

  if (NbrOfLinks > 0) {
    wxHyperlinkListNode* linknode = m_pRoutePoint->m_HyperlinkList->GetFirst();
    while (linknode) {
      Hyperlink* link = linknode->GetData();
      wxString s = wxString::Format(wxT("<a href='%s'>%s</a>"), link->Link,
                                    link->DescrText);
      GetSimpleBox()->AppendString(s);
      linknode = linknode->GetNext();
    }
  }
#else
  // Clear the list
  wxWindowList kids = m_scrolledWindowLinks->GetChildren();
  for (unsigned int i = 0; i < kids.GetCount(); i++) {
    wxWindowListNode* node = kids.Item(i);
    wxWindow* win = node->GetData();

    auto link_win = dynamic_cast<wxHyperlinkCtrl*>(win);
    if (link_win) {
      link_win->Disconnect(
          wxEVT_COMMAND_HYPERLINK,
          wxHyperlinkEventHandler(MarkInfoDlg::OnHyperLinkClick));
      link_win->Disconnect(
          wxEVT_RIGHT_DOWN,
          wxMouseEventHandler(MarkInfoDlg::m_htmlListContextMenu));
      win->Destroy();
    }
  }

  int NbrOfLinks = m_pRoutePoint->m_HyperlinkList->GetCount();
  HyperlinkList* hyperlinklist = m_pRoutePoint->m_HyperlinkList;
  if (NbrOfLinks > 0) {
    wxHyperlinkListNode* linknode = hyperlinklist->GetFirst();
    while (linknode) {
      Hyperlink* link = linknode->GetData();
      wxString Link = link->Link;
      wxString Descr = link->DescrText;

      wxHyperlinkCtrl* ctrl = new wxHyperlinkCtrl(
          m_scrolledWindowLinks, wxID_ANY, Descr, Link, wxDefaultPosition,
          wxDefaultSize, wxNO_BORDER | wxHL_CONTEXTMENU | wxHL_ALIGN_LEFT);
      ctrl->Connect(wxEVT_COMMAND_HYPERLINK,
                    wxHyperlinkEventHandler(MarkInfoDlg::OnHyperLinkClick),
                    NULL, this);
      if (!m_pRoutePoint->m_bIsInLayer)
        ctrl->Connect(wxEVT_RIGHT_DOWN,
                      wxMouseEventHandler(MarkInfoDlg::m_htmlListContextMenu),
                      NULL, this);

      bSizerLinks->Add(ctrl, 1, wxALL | wxEXPAND, 5);

      linknode = linknode->GetNext();
    }
  }

  // Integrate all of the rebuilt hyperlink controls
  m_scrolledWindowLinks->Layout();
#endif
}

void MarkInfoDlg::OnHyperLinkClick(wxHyperlinkEvent& event) {
  wxString url = event.GetURL();
  url.Replace(_T(" "), _T("%20"));
  if (g_Platform) g_Platform->platformLaunchDefaultBrowser(url);
}

void MarkInfoDlg::OnHtmlLinkClicked(wxHtmlLinkEvent& event) {
  //        Windows has trouble handling local file URLs with embedded anchor
  //        points, e.g file://testfile.html#point1 The trouble is with the
  //        wxLaunchDefaultBrowser with verb "open" Workaround is to probe the
  //        registry to get the default browser, and open directly
  //
  //        But, we will do this only if the URL contains the anchor point
  //        character '#' What a hack......

#ifdef __WXMSW__
  wxString cc = event.GetLinkInfo().GetHref().c_str();
  if (cc.Find(_T("#")) != wxNOT_FOUND) {
    wxRegKey RegKey(
        wxString(_T("HKEY_CLASSES_ROOT\\HTTP\\shell\\open\\command")));
    if (RegKey.Exists()) {
      wxString command_line;
      RegKey.QueryValue(wxString(_T("")), command_line);

      //  Remove "
      command_line.Replace(wxString(_T("\"")), wxString(_T("")));

      //  Strip arguments
      int l = command_line.Find(_T(".exe"));
      if (wxNOT_FOUND == l) l = command_line.Find(_T(".EXE"));

      if (wxNOT_FOUND != l) {
        wxString cl = command_line.Mid(0, l + 4);
        cl += _T(" ");
        cc.Prepend(_T("\""));
        cc.Append(_T("\""));
        cl += cc;
        wxExecute(cl);  // Async, so Fire and Forget...
      }
    }
  } else {
    wxString url = event.GetLinkInfo().GetHref().c_str();
    url.Replace(_T(" "), _T("%20"));
    ::wxLaunchDefaultBrowser(url);
    event.Skip();
  }
#else
  wxString url = event.GetLinkInfo().GetHref().c_str();
  url.Replace(_T(" "), _T("%20"));
  if (g_Platform) g_Platform->platformLaunchDefaultBrowser(url);

  event.Skip();
#endif
}

void MarkInfoDlg::OnLayoutResize(wxCommandEvent& event) {
  m_panelBasicProperties->Layout();
  this->Layout();
}

void MarkInfoDlg::OnDescChangedExt(wxCommandEvent& event) {
  if (m_panelDescription->IsShownOnScreen()) {
    m_textDescription->ChangeValue(m_textCtrlExtDescription->GetValue());
  }
  event.Skip();
}
void MarkInfoDlg::OnDescChangedBasic(wxCommandEvent& event) {
  if (m_panelBasicProperties->IsShownOnScreen()) {
    m_textCtrlExtDescription->ChangeValue(m_textDescription->GetValue());
  }
  event.Skip();
}

void MarkInfoDlg::OnExtDescriptionClick(wxCommandEvent& event) {
  long pos = m_textDescription->GetInsertionPoint();
  m_notebookProperties->SetSelection(1);
  m_textCtrlExtDescription->SetInsertionPoint(pos);
  event.Skip();
}

void MarkInfoDlg::OnShowWaypointNameSelectBasic(wxCommandEvent& event) {
  m_checkBoxShowNameExt->SetValue(m_checkBoxShowName->GetValue());
  event.Skip();
}
void MarkInfoDlg::OnShowWaypointNameSelectExt(wxCommandEvent& event) {
  m_checkBoxShowName->SetValue(m_checkBoxShowNameExt->GetValue());
  event.Skip();
}

void MarkInfoDlg::OnWptRangeRingsNoChange(wxCommandEvent& event) {
  if (!m_pRoutePoint->m_bIsInLayer) {
    m_textWaypointRangeRingsStep->Enable(
        (bool)(m_ChoiceWaypointRangeRingsNumber->GetSelection() != 0));
    m_PickColor->Enable(
        (bool)(m_ChoiceWaypointRangeRingsNumber->GetSelection() != 0));
  }
}

void MarkInfoDlg::OnSelectScaMinExt(wxCommandEvent& event) {
  if (!m_pRoutePoint->m_bIsInLayer) {
    m_textScaMin->Enable(m_checkBoxScaMin->GetValue());
  }
}

void MarkInfoDlg::OnPositionCtlUpdated(wxCommandEvent& event) {
  // Fetch the control values, convert to degrees
  double lat = fromDMM(m_textLatitude->GetValue());
  double lon = fromDMM(m_textLongitude->GetValue());
  if (!m_pRoutePoint->m_bIsInLayer) {
    m_pRoutePoint->SetPosition(lat, lon);
    pSelect->ModifySelectablePoint(lat, lon, (void*)m_pRoutePoint,
                                   SELTYPE_ROUTEPOINT);
  }
  // Update the mark position dynamically
  gFrame->RefreshAllCanvas();
}

void MarkInfoDlg::m_htmlListContextMenu(wxMouseEvent& event) {
#ifndef __ANDROID__
  // SimpleHtmlList->HitTest doesn't seem to work under msWin, so we use a
  // custom made version
  wxPoint pos = event.GetPosition();
  i_htmlList_item = -1;
  for (int i = 0; i < (int)GetSimpleBox()->GetCount(); i++) {
    wxRect rect = GetSimpleBox()->GetItemRect(i);
    if (rect.Contains(pos)) {
      i_htmlList_item = i;
      break;
    }
  }

  wxMenu* popup = new wxMenu();
  if ((GetSimpleBox()->GetCount()) > 0 && (i_htmlList_item > -1) &&
      (i_htmlList_item < (int)GetSimpleBox()->GetCount())) {
    popup->Append(ID_RCLK_MENU_DELETE_LINK, _("Delete"));
    popup->Append(ID_RCLK_MENU_EDIT_LINK, _("Edit"));
  }
  popup->Append(ID_RCLK_MENU_ADD_LINK, _("Add New"));

  m_contextObject = event.GetEventObject();
  popup->Connect(
      wxEVT_COMMAND_MENU_SELECTED,
      wxCommandEventHandler(MarkInfoDlg::On_html_link_popupmenu_Click), NULL,
      this);
  PopupMenu(popup);
  delete popup;
#else

  m_pEditedLink = dynamic_cast<wxHyperlinkCtrl*>(event.GetEventObject());

  if (m_pEditedLink) {
    wxString url = m_pEditedLink->GetURL();
    wxString label = m_pEditedLink->GetLabel();
    i_htmlList_item = -1;
    HyperlinkList* hyperlinklist = m_pRoutePoint->m_HyperlinkList;
    if (hyperlinklist->GetCount() > 0) {
      int i = 0;
      wxHyperlinkListNode* linknode = hyperlinklist->GetFirst();
      while (linknode) {
        Hyperlink* link = linknode->GetData();
        if (link->DescrText == label) {
          i_htmlList_item = i;
          break;
        }

        linknode = linknode->GetNext();
        i++;
      }
    }

    wxFont sFont = GetOCPNGUIScaledFont(_("Menu"));

    wxMenu* popup = new wxMenu();
    {
      wxMenuItem* menuItemDelete =
          new wxMenuItem(popup, ID_RCLK_MENU_DELETE_LINK, wxString(_("Delete")),
                         wxEmptyString, wxITEM_NORMAL);
#ifdef __WXQT__
      menuItemDelete->SetFont(sFont);
#endif
      popup->Append(menuItemDelete);

      wxMenuItem* menuItemEdit =
          new wxMenuItem(popup, ID_RCLK_MENU_EDIT_LINK, wxString(_("Edit")),
                         wxEmptyString, wxITEM_NORMAL);
#ifdef __WXQT__
      menuItemEdit->SetFont(sFont);
#endif
      popup->Append(menuItemEdit);
    }

    wxMenuItem* menuItemAdd =
        new wxMenuItem(popup, ID_RCLK_MENU_ADD_LINK, wxString(_("Add New")),
                       wxEmptyString, wxITEM_NORMAL);
#ifdef __WXQT__
    menuItemAdd->SetFont(sFont);
#endif
    popup->Append(menuItemAdd);

    m_contextObject = event.GetEventObject();
    popup->Connect(
        wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(MarkInfoDlg::On_html_link_popupmenu_Click), NULL,
        this);
    wxPoint p = m_scrolledWindowLinks->GetPosition();
    p.x += m_scrolledWindowLinks->GetSize().x / 2;
    PopupMenu(popup, p);
    delete popup;

    // m_scrolledWindowLinks->PopupMenu( m_menuLink,
    // m_pEditedLink->GetPosition().x /*+ event.GetPosition().x*/,
    // m_pEditedLink->GetPosition().y /*+ event.GetPosition().y*/ );
  }
/*
    wxPoint pos = event.GetPosition();
    i_htmlList_item = -1;
    for( int i=0; i <  (int)GetSimpleBox()->GetCount(); i++ )
    {
        wxRect rect = GetSimpleBox()->GetItemRect( i );
        if( rect.Contains( pos) ){
            i_htmlList_item = i;
            break;
        }
    }

 */
#endif
}

void MarkInfoDlg::OnAddLink(wxCommandEvent& event) {
  wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED);
  evt.SetId(ID_RCLK_MENU_ADD_LINK);

  On_html_link_popupmenu_Click(evt);
}

void MarkInfoDlg::On_html_link_popupmenu_Click(wxCommandEvent& event) {
  switch (event.GetId()) {
    case ID_RCLK_MENU_DELETE_LINK: {
      wxHyperlinkListNode* node =
          m_pRoutePoint->m_HyperlinkList->Item(i_htmlList_item);
      m_pRoutePoint->m_HyperlinkList->DeleteNode(node);
      UpdateHtmlList();
      break;
    }
    case ID_RCLK_MENU_EDIT_LINK: {
      Hyperlink* link =
          m_pRoutePoint->m_HyperlinkList->Item(i_htmlList_item)->GetData();
      LinkPropImpl* LinkPropDlg = new LinkPropImpl(this);
      LinkPropDlg->m_textCtrlLinkDescription->SetValue(link->DescrText);
      LinkPropDlg->m_textCtrlLinkUrl->SetValue(link->Link);
      DimeControl(LinkPropDlg);
      LinkPropDlg->ShowWindowModalThenDo([this, LinkPropDlg,
                                          link](int retcode) {
        if (retcode == wxID_OK) {
          link->DescrText = LinkPropDlg->m_textCtrlLinkDescription->GetValue();
          link->Link = LinkPropDlg->m_textCtrlLinkUrl->GetValue();
          m_pRoutePoint->m_HyperlinkList->Item(i_htmlList_item)->SetData(link);
          UpdateHtmlList();
        }
      });
      break;
    }
    case ID_RCLK_MENU_ADD_LINK: {
      LinkPropImpl* LinkPropDlg = new LinkPropImpl(this);
      LinkPropDlg->m_textCtrlLinkDescription->SetValue(wxEmptyString);
      LinkPropDlg->m_textCtrlLinkUrl->SetValue(wxEmptyString);
      DimeControl(LinkPropDlg);
      LinkPropDlg->ShowWindowModalThenDo([this, LinkPropDlg](int retcode) {
        if (retcode == wxID_OK) {
          Hyperlink* link = new Hyperlink;
          link->DescrText = LinkPropDlg->m_textCtrlLinkDescription->GetValue();
          link->Link = LinkPropDlg->m_textCtrlLinkUrl->GetValue();
          // Check if decent
          if (link->DescrText == wxEmptyString) {
            link->DescrText = link->Link;
          }
          if (link->Link == wxEmptyString) {
            delete link;
          } else {
            m_pRoutePoint->m_HyperlinkList->Append(link);
          }
          UpdateHtmlList();
        }
      });
      break;
    }
  }
  event.Skip();
}

void MarkInfoDlg::OnRightClickLatLon(wxCommandEvent& event) {
  wxMenu* popup = new wxMenu();
  popup->Append(ID_RCLK_MENU_COPY, _("Copy"));
  popup->Append(ID_RCLK_MENU_COPY_LL, _("Copy lat/long"));
  popup->Append(ID_RCLK_MENU_PASTE, _("Paste"));
  popup->Append(ID_RCLK_MENU_PASTE_LL, _("Paste lat/long"));
  m_contextObject = event.GetEventObject();
  popup->Connect(wxEVT_COMMAND_MENU_SELECTED,
                 wxCommandEventHandler(MarkInfoDlg::OnCopyPasteLatLon), NULL,
                 this);

  PopupMenu(popup);
  delete popup;
}

void MarkInfoDlg::OnCopyPasteLatLon(wxCommandEvent& event) {
  // Fetch the control values, convert to degrees
  double lat = fromDMM(m_textLatitude->GetValue());
  double lon = fromDMM(m_textLongitude->GetValue());

  wxString result;

  switch (event.GetId()) {
    case ID_RCLK_MENU_PASTE: {
      if (wxTheClipboard->Open()) {
        wxTextDataObject data;
        wxTheClipboard->GetData(data);
        result = data.GetText();
        ((wxTextCtrl*)m_contextObject)->SetValue(result);
        wxTheClipboard->Close();
      }
      return;
    }
    case ID_RCLK_MENU_PASTE_LL: {
      if (wxTheClipboard->Open()) {
        wxTextDataObject data;
        wxTheClipboard->GetData(data);
        result = data.GetText();

        PositionParser pparse(result);

        if (pparse.IsOk()) {
          m_textLatitude->SetValue(pparse.GetLatitudeString());
          m_textLongitude->SetValue(pparse.GetLongitudeString());
        }
        wxTheClipboard->Close();
      }
      return;
    }
    case ID_RCLK_MENU_COPY: {
      result = ((wxTextCtrl*)m_contextObject)->GetValue();
      break;
    }
    case ID_RCLK_MENU_COPY_LL: {
      result << toSDMM(1, lat, true) << _T('\t');
      result << toSDMM(2, lon, true);
      break;
    }
  }

  if (wxTheClipboard->Open()) {
    wxTextDataObject* data = new wxTextDataObject;
    data->SetText(result);
    wxTheClipboard->SetData(data);
    wxTheClipboard->Close();
  }
}

void MarkInfoDlg::DefautlBtnClicked(wxCommandEvent& event) {
  m_SaveDefaultDlg = new SaveDefaultsDialog(this);
  m_SaveDefaultDlg->Center();
  DimeControl(m_SaveDefaultDlg);
  int retcode = m_SaveDefaultDlg->ShowModal();

  {
    if (retcode == wxID_OK) {
      double value;
      if (m_SaveDefaultDlg->IconCB->GetValue()) {
        g_default_wp_icon =
            *pWayPointMan->GetIconKey(m_bcomboBoxIcon->GetSelection());
      }
      if (m_SaveDefaultDlg->RangRingsCB->GetValue()) {
        g_iWaypointRangeRingsNumber =
            m_ChoiceWaypointRangeRingsNumber->GetSelection();
        if (m_textWaypointRangeRingsStep->GetValue().ToDouble(&value))
          g_fWaypointRangeRingsStep = fromUsrDistance(value, -1);
        g_colourWaypointRangeRingsColour = m_PickColor->GetColour();
      }
      if (m_SaveDefaultDlg->ArrivalRCB->GetValue())
        if (m_textArrivalRadius->GetValue().ToDouble(&value))
          g_n_arrival_circle_radius = fromUsrDistance(value, -1);
      if (m_SaveDefaultDlg->ScaleCB->GetValue()) {
        g_iWpt_ScaMin = wxAtoi(m_textScaMin->GetValue());
        g_bUseWptScaMin = m_checkBoxScaMin->GetValue();
      }
      if (m_SaveDefaultDlg->NameCB->GetValue()) {
        g_bShowWptName = m_checkBoxShowName->GetValue();
      }
    }
    m_SaveDefaultDlg = NULL;
  }
}

void MarkInfoDlg::OnMarkInfoCancelClick(wxCommandEvent& event) {
  if (m_pRoutePoint) {
    m_pRoutePoint->SetVisible(m_bIsVisible_save);
    m_pRoutePoint->SetNameShown(m_bShowName_save);
    m_pRoutePoint->SetPosition(m_lat_save, m_lon_save);
    m_pRoutePoint->SetIconName(m_IconName_save);
    m_pRoutePoint->ReLoadIcon();
    m_pRoutePoint->SetName(m_Name_save);
    m_pRoutePoint->m_MarkDescription = m_Description_save;
    m_pRoutePoint->SetUseSca(m_bUseScaMin_save);
    m_pRoutePoint->SetScaMin(m_iScaminVal_save);

    m_pRoutePoint->m_HyperlinkList->Clear();

    int NbrOfLinks = m_pMyLinkList->GetCount();
    if (NbrOfLinks > 0) {
      wxHyperlinkListNode* linknode = m_pMyLinkList->GetFirst();
      while (linknode) {
        Hyperlink* link = linknode->GetData();
        Hyperlink* h = new Hyperlink();
        h->DescrText = link->DescrText;
        h->Link = link->Link;
        h->LType = link->LType;

        m_pRoutePoint->m_HyperlinkList->Append(h);

        linknode = linknode->GetNext();
      }
    }
  }

  m_lasttspos.Clear();

#ifdef __WXGTK__
  gFrame->Raise();
#endif

  Show(false);
  delete m_pMyLinkList;
  m_pMyLinkList = NULL;
  SetClientSize(m_defaultClientSize);

#ifdef __ANDROID__
  androidEnableBackButton(true);
#endif

  event.Skip();
}

void MarkInfoDlg::OnMarkInfoOKClick(wxCommandEvent& event) {
  if (m_pRoutePoint) {
    m_pRoutePoint->m_wxcWaypointRangeRingsColour = m_PickColor->GetColour();

    OnPositionCtlUpdated(event);
    SaveChanges();  // write changes to globals and update config
  }

#ifdef __WXGTK__
  gFrame->Raise();
#endif

  Show(false);

  if (pRouteManagerDialog && pRouteManagerDialog->IsShown())
    pRouteManagerDialog->UpdateWptListCtrl();

  if (pRoutePropDialog && pRoutePropDialog->IsShown())
    pRoutePropDialog->UpdatePoints();

  SetClientSize(m_defaultClientSize);

#ifdef __ANDROID__
  androidEnableBackButton(true);
#endif

  event.Skip();
}

bool MarkInfoDlg::UpdateProperties(bool positionOnly) {
  if (m_pRoutePoint) {
    m_textLatitude->SetValue(::toSDMM(1, m_pRoutePoint->m_lat));
    m_textLongitude->SetValue(::toSDMM(2, m_pRoutePoint->m_lon));
    m_lat_save = m_pRoutePoint->m_lat;
    m_lon_save = m_pRoutePoint->m_lon;
    m_textName->SetValue(m_pRoutePoint->GetName());
    m_textDescription->ChangeValue(m_pRoutePoint->m_MarkDescription);
    m_textCtrlExtDescription->ChangeValue(m_pRoutePoint->m_MarkDescription);
    m_checkBoxShowName->SetValue(m_pRoutePoint->m_bShowName);
    m_checkBoxShowNameExt->SetValue(m_pRoutePoint->m_bShowName);
    m_checkBoxVisible->SetValue(m_pRoutePoint->m_bIsVisible);
    m_checkBoxScaMin->SetValue(m_pRoutePoint->GetUseSca());
    m_textScaMin->SetValue(
        wxString::Format(wxT("%i"), (int)m_pRoutePoint->GetScaMin()));
    m_textCtrlGuid->SetValue(m_pRoutePoint->m_GUID);
    m_ChoiceWaypointRangeRingsNumber->SetSelection(
        m_pRoutePoint->GetWaypointRangeRingsNumber());
    wxString buf;
    buf.Printf(_T("%.3f"),
               toUsrDistance(m_pRoutePoint->GetWaypointRangeRingsStep(), -1));
    m_textWaypointRangeRingsStep->SetValue(buf);
    m_staticTextArrivalUnits->SetLabel(getUsrDistanceUnit());
    buf.Printf(_T("%.3f"),
               toUsrDistance(m_pRoutePoint->GetWaypointArrivalRadius(), -1));
    m_textArrivalRadius->SetValue(buf);

    int nUnits = m_pRoutePoint->GetWaypointRangeRingsStepUnits();
    m_RangeRingUnits->SetSelection(nUnits);

    wxColour col = m_pRoutePoint->m_wxcWaypointRangeRingsColour;
    m_PickColor->SetColour(col);

    if (m_pRoutePoint->m_bIsInRoute) {
      if (m_name_validator) m_name_validator.reset();
      m_name_validator =
          std::make_unique<RoutePointNameValidator>(m_pRoutePoint);
      m_textName->SetValidator(*m_name_validator);
      m_textName->Bind(wxEVT_TEXT, &TextField::OnTextChanged, m_textName);
      m_textName->Bind(wxEVT_KILL_FOCUS, &MarkInfoDlg::OnFocusEvent, this);
    } else {
      m_textName->SetValidator();
      m_textName->Unbind(wxEVT_TEXT, &TextField::OnTextChanged, m_textName);
      m_textName->Unbind(wxEVT_KILL_FOCUS, &MarkInfoDlg::OnFocusEvent, this);
    }

    if (m_comboBoxTideStation->GetStringSelection() !=
        m_pRoutePoint->m_TideStation) {
      m_comboBoxTideStation->Clear();
      m_comboBoxTideStation->Append(wxEmptyString);
      if (!m_pRoutePoint->m_TideStation.IsEmpty()) {
        m_comboBoxTideStation->Append(m_pRoutePoint->m_TideStation);
        m_comboBoxTideStation->SetSelection(1);
      }
    }

    m_staticTextPlSpeedUnits->SetLabel(getUsrSpeedUnit());
    if (m_pRoutePoint->GetPlannedSpeed() > .01) {
      m_textCtrlPlSpeed->SetValue(wxString::Format(
          "%.1f", toUsrSpeed(m_pRoutePoint->GetPlannedSpeed())));
    } else {
      m_textCtrlPlSpeed->SetValue(wxEmptyString);
    }

    bool isLastWaypoint = false;
    if (m_pRoutePoint && m_pRoutePoint->m_bIsInRoute) {
      // Get routes containing this waypoint
      wxArrayPtrVoid* pRouteArray =
          g_pRouteMan->GetRouteArrayContaining(m_pRoutePoint);
      if (pRouteArray) {
        isLastWaypoint = true;
        // Check if this waypoint is the last across all routes.
        for (unsigned int i = 0; i < pRouteArray->GetCount(); i++) {
          Route* route = (Route*)pRouteArray->Item(i);
          if (route->GetLastPoint()->m_GUID != m_pRoutePoint->m_GUID) {
            isLastWaypoint = false;
            break;
          }
        }
        delete pRouteArray;
      }
    }
    wxDateTime etd;
    etd = m_pRoutePoint->GetManualETD();
    if (isLastWaypoint) {
      // If this is the last waypoint in a route, uncheck the checkbox and set
      // the date/time to empty, as the ETD is meaningless.
      etd = wxDateTime();
    }
    if (etd.IsValid()) {
      m_cbEtdPresent->SetValue(true);
      wxString dtFormat = ocpn::getUsrDateTimeFormat();
      if (dtFormat == "Local Time") {
        // The ETD is in UTC and needs to be converted to local time for display
        // purpose.
        etd.MakeFromUTC();
      } else if (dtFormat == "UTC") {
        // The date/time is already in UTC.
      } else {
        // This code path is not expected to be reached, unless
        // the global date/time format is enhanced in the future
        // to include new format options.
        wxLogError(
            "MarkInfoDlg::UpdateProperties. Unexpected date/time format: %s",
            dtFormat);
        etd = wxInvalidDateTime;
      }
      m_EtdDatePickerCtrl->SetValue(etd.GetDateOnly());
      m_EtdTimePickerCtrl->SetValue(etd);
    } else {
      m_cbEtdPresent->SetValue(false);
    }
    // Inherit the date/time format from the user settings.
    m_staticTextEtd->SetLabel(
        wxString::Format("%s (%s)", _("ETD"), ocpn::getUsrDateTimeFormat()));

    m_staticTextPlSpeed->Show(m_pRoutePoint->m_bIsInRoute);
    m_textCtrlPlSpeed->Show(m_pRoutePoint->m_bIsInRoute);
    m_staticTextEtd->Show(m_pRoutePoint->m_bIsInRoute);
    m_EtdDatePickerCtrl->Show(m_pRoutePoint->m_bIsInRoute);
    m_EtdTimePickerCtrl->Show(m_pRoutePoint->m_bIsInRoute);
    m_cbEtdPresent->Show(m_pRoutePoint->m_bIsInRoute);
    m_staticTextPlSpeedUnits->Show(m_pRoutePoint->m_bIsInRoute);
    m_staticTextArrivalRadius->Show(m_pRoutePoint->m_bIsInRoute);
    m_staticTextArrivalUnits->Show(m_pRoutePoint->m_bIsInRoute);
    m_textArrivalRadius->Show(m_pRoutePoint->m_bIsInRoute);

    if (positionOnly) return true;

    // Layer or not?
    if (m_pRoutePoint->m_bIsInLayer) {
      m_staticTextLayer->Enable();
      m_staticTextLayer->Show(true);
      m_textName->SetEditable(false);
      m_textDescription->SetEditable(false);
      m_textCtrlExtDescription->SetEditable(false);
      m_textLatitude->SetEditable(false);
      m_textLongitude->SetEditable(false);
      m_bcomboBoxIcon->Enable(false);
      m_checkBoxShowName->Enable(false);
      m_checkBoxVisible->Enable(false);
      m_textArrivalRadius->SetEditable(false);
      m_checkBoxScaMin->Enable(false);
      m_textScaMin->SetEditable(false);
      m_checkBoxShowNameExt->Enable(false);
      m_ChoiceWaypointRangeRingsNumber->Enable(false);
      m_textWaypointRangeRingsStep->SetEditable(false);
      m_PickColor->Enable(false);
      DefaultsBtn->Enable(false);
      m_EtdDatePickerCtrl->Enable(false);
      m_EtdTimePickerCtrl->Enable(false);
      m_cbEtdPresent->Enable(false);
      m_notebookProperties->SetSelection(0);  // Show Basic page
      m_comboBoxTideStation->Enable(false);
    } else {
      m_staticTextLayer->Enable(false);
      m_staticTextLayer->Show(false);
      m_textName->SetEditable(true);
      m_textDescription->SetEditable(true);
      m_textCtrlExtDescription->SetEditable(true);
      m_textLatitude->SetEditable(true);
      m_textLongitude->SetEditable(true);
      m_bcomboBoxIcon->Enable(true);
      m_checkBoxShowName->Enable(true);
      m_checkBoxVisible->Enable(true);
      m_textArrivalRadius->SetEditable(true);
      m_checkBoxScaMin->Enable(true);
      m_textScaMin->SetEditable(true);
      m_checkBoxShowNameExt->Enable(true);
      m_ChoiceWaypointRangeRingsNumber->Enable(true);
      m_textWaypointRangeRingsStep->SetEditable(true);
      m_PickColor->Enable(true);
      DefaultsBtn->Enable(true);
      m_notebookProperties->SetSelection(0);
      m_comboBoxTideStation->Enable(true);

      // If this is the last waypoint in a route, disable the ETD as it does not
      // make sense to have an ETD for the last waypoint in a route.
      m_EtdDatePickerCtrl->Enable(!isLastWaypoint);
      m_EtdTimePickerCtrl->Enable(!isLastWaypoint);
      m_cbEtdPresent->Enable(!isLastWaypoint);
    }

    // Fill the icon selector combo box
    m_bcomboBoxIcon->Clear();
    //      Iterate on the Icon Descriptions, filling in the combo control
    bool fillCombo = m_bcomboBoxIcon->GetCount() == 0;

    if (fillCombo) {
      for (int i = 0; i < pWayPointMan->GetNumIcons(); i++) {
        wxString* ps = pWayPointMan->GetIconDescription(i);
        wxBitmap bmp =
            pWayPointMan->GetIconBitmapForList(i, 2 * GetCharHeight());

        m_bcomboBoxIcon->Append(*ps, bmp);
      }
    }
    // find the correct item in the combo box
    int iconToSelect = -1;
    for (int i = 0; i < pWayPointMan->GetNumIcons(); i++) {
      if (*pWayPointMan->GetIconKey(i) == m_pRoutePoint->GetIconName()) {
        iconToSelect = i;
        m_bcomboBoxIcon->Select(iconToSelect);
        break;
      }
    }
    wxCommandEvent ev;
    OnShowWaypointNameSelectBasic(ev);
    OnWptRangeRingsNoChange(ev);
    OnSelectScaMinExt(ev);
    UpdateHtmlList();
  }

#ifdef __ANDROID__
  androidEnableBackButton(false);
#endif

  Fit();
  // SetMinSize(wxSize(-1, 600));
  RecalculateSize();

  return true;
}

// Focus event handler to validate the dialog.
void MarkInfoDlg::OnFocusEvent(wxFocusEvent& event) {
  bool is_valid = Validate();
  m_buttonOkay->Enable(is_valid);
  event.Skip();
}

void MarkInfoDlg::OnBitmapCombClick(wxCommandEvent& event) {
  wxString* icon_name =
      pWayPointMan->GetIconKey(m_bcomboBoxIcon->GetSelection());
  if (icon_name && icon_name->Length()) m_pRoutePoint->SetIconName(*icon_name);
  m_pRoutePoint->ReLoadIcon();
  SaveChanges();
  // pConfig->UpdateWayPoint( m_pRoutePoint );
}

void MarkInfoDlg::ValidateMark(void) {
  //    Look in the master list of Waypoints to see if the currently selected
  //    waypoint is still valid It may have been deleted as part of a route
  wxRoutePointListNode* node = pWayPointMan->GetWaypointList()->GetFirst();

  bool b_found = false;
  while (node) {
    RoutePoint* rp = node->GetData();
    if (m_pRoutePoint == rp) {
      b_found = true;
      break;
    }
    node = node->GetNext();
  }
  if (!b_found) m_pRoutePoint = NULL;
}

bool MarkInfoDlg::SaveChanges() {
  if (m_pRoutePoint) {
    if (m_pRoutePoint->m_bIsInLayer) return true;
    if (!this->Validate()) return false;  // prevent invalid save

    // Get User input Text Fields
    m_pRoutePoint->SetName(m_textName->GetValue());
    m_pRoutePoint->SetWaypointArrivalRadius(m_textArrivalRadius->GetValue());
    m_pRoutePoint->SetScaMin(m_textScaMin->GetValue());
    m_pRoutePoint->SetUseSca(m_checkBoxScaMin->GetValue());
    m_pRoutePoint->m_MarkDescription = m_textDescription->GetValue();
    m_pRoutePoint->SetVisible(m_checkBoxVisible->GetValue());
    m_pRoutePoint->m_bShowName = m_checkBoxShowName->GetValue();
    m_pRoutePoint->SetPosition(fromDMM(m_textLatitude->GetValue()),
                               fromDMM(m_textLongitude->GetValue()));
    wxString* icon_name =
        pWayPointMan->GetIconKey(m_bcomboBoxIcon->GetSelection());
    if (icon_name && icon_name->Length())
      m_pRoutePoint->SetIconName(*icon_name);
    m_pRoutePoint->ReLoadIcon();
    m_pRoutePoint->SetShowWaypointRangeRings(
        (bool)(m_ChoiceWaypointRangeRingsNumber->GetSelection() != 0));
    m_pRoutePoint->SetWaypointRangeRingsNumber(
        m_ChoiceWaypointRangeRingsNumber->GetSelection());
    double value;
    if (m_textWaypointRangeRingsStep->GetValue().ToDouble(&value))
      m_pRoutePoint->SetWaypointRangeRingsStep(fromUsrDistance(value, -1));
    if (m_textArrivalRadius->GetValue().ToDouble(&value))
      m_pRoutePoint->SetWaypointArrivalRadius(fromUsrDistance(value, -1));

    if (m_RangeRingUnits->GetSelection() != wxNOT_FOUND)
      m_pRoutePoint->SetWaypointRangeRingsStepUnits(
          m_RangeRingUnits->GetSelection());

    m_pRoutePoint->m_TideStation = m_comboBoxTideStation->GetStringSelection();
    if (m_textCtrlPlSpeed->GetValue() == wxEmptyString) {
      m_pRoutePoint->SetPlannedSpeed(0.0);
    } else {
      double spd;
      if (m_textCtrlPlSpeed->GetValue().ToDouble(&spd)) {
        m_pRoutePoint->SetPlannedSpeed(fromUsrSpeed(spd));
      }
    }

    if (m_cbEtdPresent->GetValue()) {
      wxDateTime dt = m_EtdDatePickerCtrl->GetValue();
      wxDateTime t = m_EtdTimePickerCtrl->GetValue();
      int hour = t.GetHour();
      dt.SetHour(hour);
      dt.SetMinute(m_EtdTimePickerCtrl->GetValue().GetMinute());
      dt.SetSecond(m_EtdTimePickerCtrl->GetValue().GetSecond());
      if (dt.IsValid()) {
        // The date/time in the UI is specified according to the global settings
        // in Options -> Date/Time format. The date/time format is either "Local
        // Time" or "UTC". If the date/time format is "Local Time", convert to
        // UTC. Otherwise, it is already in UTC.
        wxString dtFormat = ocpn::getUsrDateTimeFormat();
        if (dtFormat == "Local Time") {
          m_pRoutePoint->SetETD(dt.MakeUTC());
        } else if (dtFormat == "UTC") {
          m_pRoutePoint->SetETD(dt);
        } else {
          // This code path should never be reached, as the date/time format is
          // either "Local Time" or "UTC".
          // In the future, other date/time formats may be supported in
          // global settings (Options -> Display -> Date/Time Format).
          // When/if this happens, this code will need to be updated to
          // handle the new formats.
          wxLogError(
              "Failed to configured ETD. Unsupported date/time format: %s",
              dtFormat);
          m_pRoutePoint->SetETD(wxInvalidDateTime);
        }
      }
    } else {
      m_pRoutePoint->SetETD(wxInvalidDateTime);
    }

    if (m_pRoutePoint->m_bIsInRoute) {
      // Update the route segment selectables
      pSelect->UpdateSelectableRouteSegments(m_pRoutePoint);

      // Get an array of all routes using this point
      wxArrayPtrVoid* pEditRouteArray =
          g_pRouteMan->GetRouteArrayContaining(m_pRoutePoint);

      if (pEditRouteArray) {
        for (unsigned int ir = 0; ir < pEditRouteArray->GetCount(); ir++) {
          Route* pr = (Route*)pEditRouteArray->Item(ir);
          pr->FinalizeForRendering();
          pr->UpdateSegmentDistances();

          // pConfig->UpdateRoute(pr);
          NavObj_dB::GetInstance().UpdateRoute(pr);
        }
        delete pEditRouteArray;
      }
    } else {
      // pConfig->UpdateWayPoint(m_pRoutePoint);
      NavObj_dB::GetInstance().UpdateRoutePoint(m_pRoutePoint);
    }
  }
  return true;
}

SaveDefaultsDialog::SaveDefaultsDialog(MarkInfoDlg* parent)
    : wxDialog(parent, wxID_ANY, _("Save some defaults")) {
  //(*Initialize(SaveDefaultsDialog)
  this->SetSizeHints(wxDefaultSize, wxDefaultSize);

  wxBoxSizer* bSizer1 = new wxBoxSizer(wxVERTICAL);
  wxStdDialogButtonSizer* StdDialogButtonSizer1;

  StaticText1 =
      new wxStaticText(this, wxID_ANY,
                       _("Check which properties of current waypoint\n should "
                         "be set as default for NEW waypoints."));
  bSizer1->Add(StaticText1, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

  wxFlexGridSizer* fgSizer1 = new wxFlexGridSizer(2);

  wxString s =
      (g_pMarkInfoDialog->m_checkBoxShowName->GetValue() ? _("Do use")
                                                         : _("Don't use"));
  NameCB =
      new wxCheckBox(this, wxID_ANY, _("Show Waypoint Name"), wxDefaultPosition,
                     wxDefaultSize, 0, wxDefaultValidator);
  fgSizer1->Add(NameCB, 0, wxALL, 5);
  stName = new wxStaticText(this, wxID_ANY, _T("[") + s + _T("]"),
                            wxDefaultPosition, wxDefaultSize, 0);
  stName->Wrap(-1);
  fgSizer1->Add(stName, 0, wxALL | wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL, 5);

  s = g_pMarkInfoDialog->m_pRoutePoint->GetIconName();
  IconCB = new wxCheckBox(this, wxID_ANY, _("Icon"));
  fgSizer1->Add(IconCB, 0, wxALL, 5);
  stIcon = new wxStaticText(this, wxID_ANY, _T("[") + s + _T("]"),
                            wxDefaultPosition, wxDefaultSize, 0);
  stIcon->Wrap(-1);
  fgSizer1->Add(stIcon, 0, wxALL | wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL, 5);

  s = (g_pMarkInfoDialog->m_ChoiceWaypointRangeRingsNumber->GetSelection()
           ? _("Do use") +
                 wxString::Format(
                     _T(" (%i) "),
                     g_pMarkInfoDialog->m_ChoiceWaypointRangeRingsNumber
                         ->GetSelection())
           : _("Don't use"));
  RangRingsCB = new wxCheckBox(this, wxID_ANY, _("Range rings"));
  fgSizer1->Add(RangRingsCB, 0, wxALL, 5);
  stRR = new wxStaticText(this, wxID_ANY, _T("[") + s + _T("]"),
                          wxDefaultPosition, wxDefaultSize, 0);
  stRR->Wrap(-1);
  fgSizer1->Add(stRR, 0, wxALL | wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL, 5);

  s = (g_pMarkInfoDialog->m_textArrivalRadius->GetValue());
  ArrivalRCB = new wxCheckBox(this, wxID_ANY, _("Arrival radius"));
  fgSizer1->Add(ArrivalRCB, 0, wxALL, 5);
  stArrivalR = new wxStaticText(
      this, wxID_ANY,
      wxString::Format(_T("[%s %s]"), s.c_str(), getUsrDistanceUnit().c_str()),
      wxDefaultPosition, wxDefaultSize, 0);
  stArrivalR->Wrap(-1);
  fgSizer1->Add(stArrivalR, 0, wxALL | wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL,
                5);

  s = (g_pMarkInfoDialog->m_checkBoxScaMin->GetValue()
           ? _("Show only if") + _T(" < ") +
                 g_pMarkInfoDialog->m_textScaMin->GetValue()
           : _("Show always"));
  ScaleCB = new wxCheckBox(this, wxID_ANY, _("Show only at scale"));
  fgSizer1->Add(ScaleCB, 0, wxALL, 5);
  stScale = new wxStaticText(this, wxID_ANY, _T("[") + s + _T("]"),
                             wxDefaultPosition, wxDefaultSize, 0);
  stScale->Wrap(-1);
  fgSizer1->Add(stScale, 0, wxALL | wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL, 5);

  bSizer1->Add(fgSizer1, 0, wxALL | wxEXPAND, 5);

  StdDialogButtonSizer1 = new wxStdDialogButtonSizer();
  StdDialogButtonSizer1->AddButton(new wxButton(this, wxID_OK));
  StdDialogButtonSizer1->AddButton(
      new wxButton(this, wxID_CANCEL, _("Cancel")));
  StdDialogButtonSizer1->Realize();
  bSizer1->Add(StdDialogButtonSizer1, 0, wxALL | wxEXPAND, 5);

  SetSizer(bSizer1);
  Fit();
  Layout();

#ifdef __ANDROID__
  SetSize(parent->GetSize());
#endif

  Center();
}

void MarkInfoDlg::ShowTidesBtnClicked(wxCommandEvent& event) {
  if (m_comboBoxTideStation->GetSelection() < 1) {
    return;
  }
  IDX_entry* pIDX = (IDX_entry*)ptcmgr->GetIDX_entry(
      ptcmgr->GetStationIDXbyName(m_comboBoxTideStation->GetStringSelection(),
                                  fromDMM(m_textLatitude->GetValue()),
                                  fromDMM(m_textLongitude->GetValue())));
  if (pIDX) {
    TCWin* pCwin = new TCWin(gFrame->GetPrimaryCanvas(), 0, 0, pIDX);
    pCwin->Show();
  } else {
    wxString msg(_("Tide Station not found"));
    msg += _T(":\n");
    msg += m_comboBoxTideStation->GetStringSelection();
    OCPNMessageBox(NULL, msg, _("OpenCPN Info"), wxOK | wxCENTER, 10);
  }
}
