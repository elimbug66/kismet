/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <config.h>

#include <stdio.h>

#include <string>
#include <sstream>

#include <globalregistry.h>
#include <kis_panel_plugin.h>
#include <kis_panel_frontend.h>
#include <kis_panel_windows.h>
#include <kis_panel_network.h>
#include <kis_panel_widgets.h>
#include <version.h>

#include "tracker_btscan.h"

const char *btscandev_fields[] = {
	"bdaddr", "name", "class", "firsttime", "lasttime", "packets",
	NULL
};

enum btscan_sort_type {
	btscan_sort_bdaddr, btscan_sort_bdname, btscan_sort_bdclass,
	btscan_sort_firsttime, btscan_sort_lasttime, btscan_sort_packets
};

struct btscan_data {
	int mi_plugin_btscan, mi_showbtscan;

	int mn_sub_sort, mi_sort_bdaddr, mi_sort_bdname, mi_sort_bdclass,
		mi_sort_firsttime, mi_sort_lasttime, mi_sort_packets;

	map<mac_addr, btscan_network *> btdev_map;
	vector<btscan_network *> btdev_vec;

	Kis_Scrollable_Table *btdevlist;

	int cliaddref;

	int timerid;

	string asm_btscandev_fields;
	int asm_btscandev_num;

	btscan_sort_type sort_type;

	KisPanelPluginData *pdata;
	Kis_Menu *menu;
};

class Btscan_Sort_Bdaddr {
public:
	inline bool operator()(btscan_network *x, btscan_network *y) const {
		return x->bd_addr < y->bd_addr;
	}
};

class Btscan_Sort_Firsttime {
public:
	inline bool operator()(btscan_network *x, btscan_network *y) const {
		return x->first_time < y->first_time;
	}
};

class Btscan_Sort_Lasttime {
public:
	inline bool operator()(btscan_network *x, btscan_network *y) const {
		return x->last_time < y->last_time;
	}
};

class Btscan_Sort_Class {
public:
	inline bool operator()(btscan_network *x, btscan_network *y) const {
		return x->bd_class < y->bd_class;
	}
};

class Btscan_Sort_Name {
public:
	inline bool operator()(btscan_network *x, btscan_network *y) const {
		return x->bd_name < y->bd_name;
	}
};

class Btscan_Sort_Packets {
public:
	inline bool operator()(btscan_network *x, btscan_network *y) const {
		return x->packets < y->packets;
	}
};

// Menu events
int Btscan_plugin_menu_cb(void *auxptr);
void Btscan_show_menu_cb(MENUITEM_CB_PARMS);
void Btscan_sort_menu_cb(MENUITEM_CB_PARMS);

// Network events
void BtscanCliAdd(KPI_ADDCLI_CB_PARMS);
void BtscanCliConfigured(CLICONF_CB_PARMS);

// List select
void BtscanDevlistCB(COMPONENT_CALLBACK_PARMS);

// List content timer
int BtscanTimer(TIMEEVENT_PARMS);

extern "C" {

int panel_plugin_init(GlobalRegistry *globalreg, KisPanelPluginData *pdata) {
	_MSG("Loading Kismet BTSCAN plugin", MSGFLAG_INFO);

	btscan_data *btscan = new btscan_data;

	pdata->pluginaux = (void *) btscan;

	btscan->pdata = pdata;

	btscan->sort_type = btscan_sort_bdaddr;

	btscan->asm_btscandev_num = 
		TokenNullJoin(&(btscan->asm_btscandev_fields), btscandev_fields);

	btscan->mi_plugin_btscan =
		pdata->mainpanel->AddPluginMenuItem("BT Scan", Btscan_plugin_menu_cb, pdata);

	btscan->btdevlist = new Kis_Scrollable_Table(globalreg, pdata->mainpanel);

	vector<Kis_Scrollable_Table::title_data> titles;
	Kis_Scrollable_Table::title_data t;

	t.width = 17;
	t.title = "BD Addr";
	t.alignment = 0;
	titles.push_back(t);

	t.width = 15;
	t.title = "Name";
	t.alignment = 0;
	titles.push_back(t);

	t.width = 9;
	t.title = "Class";
	t.alignment = 0;
	titles.push_back(t);

	btscan->btdevlist->AddTitles(titles);
	btscan->btdevlist->SetPreferredSize(0, 10);

	btscan->btdevlist->SetHighlightSelected(1);
	btscan->btdevlist->SetLockScrollTop(1);
	btscan->btdevlist->SetDrawTitles(1);

	pdata->mainpanel->AddComponentVec(btscan->btdevlist, 
									  (KIS_PANEL_COMP_DRAW | KIS_PANEL_COMP_EVT |
									   KIS_PANEL_COMP_TAB));
	pdata->mainpanel->FetchNetBox()->Pack_After_Named("KIS_MAIN_NETLIST",
													  btscan->btdevlist, 1, 0);

	btscan->menu = pdata->kpinterface->FetchMainPanel()->FetchMenu();
	int mn_view = btscan->menu->FindMenu("View");

	pdata->kpinterface->FetchMainPanel()->AddViewSeparator();
	btscan->mi_showbtscan = btscan->menu->AddMenuItem("BT Scan", mn_view, 0);
	btscan->menu->SetMenuItemCallback(btscan->mi_showbtscan, Btscan_show_menu_cb, 
									  btscan);

	pdata->kpinterface->FetchMainPanel()->AddSortSeparator();
	int mn_sort = btscan->menu->FindMenu("Sort");
	btscan->mn_sub_sort = btscan->menu->AddSubMenuItem("BTScan", mn_sort, 0);
	btscan->mi_sort_bdaddr = 
		btscan->menu->AddMenuItem("BD Addr", btscan->mn_sub_sort, 0);
	btscan->mi_sort_bdname = btscan->menu->AddMenuItem("Name", btscan->mn_sub_sort, 0);
	btscan->mi_sort_bdclass = btscan->menu->AddMenuItem("Class", btscan->mn_sub_sort, 0);
	btscan->mi_sort_firsttime = 
		btscan->menu->AddMenuItem("First Time", btscan->mn_sub_sort, 0);
	btscan->mi_sort_lasttime = 
		btscan->menu->AddMenuItem("Last Time", btscan->mn_sub_sort, 0);
	btscan->mi_sort_packets = 
		btscan->menu->AddMenuItem("Times Seen", btscan->mn_sub_sort, 0);

	btscan->menu->SetMenuItemCallback(btscan->mi_sort_bdaddr, Btscan_sort_menu_cb, 
									  btscan);
	btscan->menu->SetMenuItemCallback(btscan->mi_sort_bdname, Btscan_sort_menu_cb, 
									  btscan);
	btscan->menu->SetMenuItemCallback(btscan->mi_sort_bdclass, Btscan_sort_menu_cb, 
									  btscan);
	btscan->menu->SetMenuItemCallback(btscan->mi_sort_firsttime, Btscan_sort_menu_cb, 
									  btscan);
	btscan->menu->SetMenuItemCallback(btscan->mi_sort_lasttime, Btscan_sort_menu_cb, 
									  btscan);
	btscan->menu->SetMenuItemCallback(btscan->mi_sort_packets, Btscan_sort_menu_cb, 
									  btscan);

	string opt = StrLower(pdata->kpinterface->prefs->FetchOpt("PLUGIN_BTSCAN_SHOW"));
	if (opt == "true" || opt == "") {
		btscan->btdevlist->Show();
		btscan->menu->SetMenuItemChecked(btscan->mi_showbtscan, 1);

		btscan->menu->EnableMenuItem(btscan->mi_sort_bdaddr);
		btscan->menu->EnableMenuItem(btscan->mi_sort_bdname);
		btscan->menu->EnableMenuItem(btscan->mi_sort_bdclass);
		btscan->menu->EnableMenuItem(btscan->mi_sort_firsttime);
		btscan->menu->EnableMenuItem(btscan->mi_sort_lasttime);
		btscan->menu->EnableMenuItem(btscan->mi_sort_packets);

	} else {
		btscan->btdevlist->Hide();
		btscan->menu->SetMenuItemChecked(btscan->mi_showbtscan, 0);

		btscan->menu->DisableMenuItem(btscan->mi_sort_bdaddr);
		btscan->menu->DisableMenuItem(btscan->mi_sort_bdname);
		btscan->menu->DisableMenuItem(btscan->mi_sort_bdclass);
		btscan->menu->DisableMenuItem(btscan->mi_sort_firsttime);
		btscan->menu->DisableMenuItem(btscan->mi_sort_lasttime);
		btscan->menu->DisableMenuItem(btscan->mi_sort_packets);
	}

	opt = pdata->kpinterface->prefs->FetchOpt("PLUGIN_BTSCAN_SORT");
	if (opt == "bdaddr") {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdaddr, 1);
		btscan->sort_type = btscan_sort_bdaddr;
	} else if (opt == "bdname") {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdname, 1);
		btscan->sort_type = btscan_sort_bdname;
	} else if (opt == "bdclass") {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdclass, 1);
		btscan->sort_type = btscan_sort_bdclass;
	} else if (opt == "firsttime") {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_firsttime, 1);
		btscan->sort_type = btscan_sort_firsttime;
	} else if (opt == "lasttime") {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_lasttime, 1);
		btscan->sort_type = btscan_sort_lasttime;
	} else if (opt == "packets") {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_packets, 1);
		btscan->sort_type = btscan_sort_packets;
	} else {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdaddr, 1);
		btscan->sort_type = btscan_sort_bdaddr;
	}

	// Register the timer event for populating the array
	btscan->timerid = 
		globalreg->timetracker->RegisterTimer(SERVER_TIMESLICES_SEC, NULL,
											  1, &BtscanTimer, btscan);

	// Do this LAST.  The configure event is responsible for clearing out the
	// list on reconnect, but MAY be called immediately upon being registered
	// if the client is already valid.  We have to have made all the other
	// bits first before it's valid to call this
	btscan->cliaddref =
		pdata->kpinterface->Add_NetCli_AddCli_CB(BtscanCliAdd, (void *) btscan);

	return 1;
}

// Plugin version control
void kis_revision_info(panel_plugin_revision *prev) {
	if (prev->version_api_revision >= 1) {
		prev->version_api_revision = 1;
		prev->major = string(VERSION_MAJOR);
		prev->minor = string(VERSION_MINOR);
		prev->tiny = string(VERSION_TINY);
	}
}

}

int Btscan_plugin_menu_cb(void *auxptr) {
	KisPanelPluginData *pdata = (KisPanelPluginData *) auxptr;

	pdata->kpinterface->RaiseAlert("BT Scan",
			"BT Scan UI " + string(VERSION_MAJOR) + "-" + string(VERSION_MINOR) + "-" +
				string(VERSION_TINY) + "\n"
			"\n"
			"Display Bluetooth/802.15.1 devices found by the\n"
			"BTSCAN active scanning Kismet plugin\n");
	return 1;
}

void Btscan_show_menu_cb(MENUITEM_CB_PARMS) {
	btscan_data *btscan = (btscan_data *) auxptr;

	if (btscan->pdata->kpinterface->prefs->FetchOpt("PLUGIN_BTSCAN_SHOW") == "true" ||
		btscan->pdata->kpinterface->prefs->FetchOpt("PLUGIN_BTSCAN_SHOW") == "") {

		btscan->pdata->kpinterface->prefs->SetOpt("PLUGIN_BTSCAN_SHOW", "false", 1);

		btscan->btdevlist->Hide();

		btscan->menu->DisableMenuItem(btscan->mi_sort_bdaddr);
		btscan->menu->DisableMenuItem(btscan->mi_sort_bdname);
		btscan->menu->DisableMenuItem(btscan->mi_sort_bdclass);
		btscan->menu->DisableMenuItem(btscan->mi_sort_firsttime);
		btscan->menu->DisableMenuItem(btscan->mi_sort_lasttime);
		btscan->menu->DisableMenuItem(btscan->mi_sort_packets);

		btscan->menu->SetMenuItemChecked(btscan->mi_showbtscan, 0);
	} else {
		btscan->pdata->kpinterface->prefs->SetOpt("PLUGIN_BTSCAN_SHOW", "true", 1);

		btscan->btdevlist->Show();

		btscan->menu->EnableMenuItem(btscan->mi_sort_bdaddr);
		btscan->menu->EnableMenuItem(btscan->mi_sort_bdname);
		btscan->menu->EnableMenuItem(btscan->mi_sort_bdclass);
		btscan->menu->EnableMenuItem(btscan->mi_sort_firsttime);
		btscan->menu->EnableMenuItem(btscan->mi_sort_lasttime);
		btscan->menu->EnableMenuItem(btscan->mi_sort_packets);

		btscan->menu->SetMenuItemChecked(btscan->mi_showbtscan, 1);
	}

	return;
}

void Btscan_sort_menu_cb(MENUITEM_CB_PARMS) {
	btscan_data *btscan = (btscan_data *) auxptr;

	btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdaddr, 0);
	btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdname, 0);
	btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdclass, 0);
	btscan->menu->SetMenuItemChecked(btscan->mi_sort_firsttime, 0);
	btscan->menu->SetMenuItemChecked(btscan->mi_sort_lasttime, 0);
	btscan->menu->SetMenuItemChecked(btscan->mi_sort_packets, 0);

	if (menuitem == btscan->mi_sort_bdaddr) {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdaddr, 1);
		btscan->pdata->kpinterface->prefs->SetOpt("PLUGIN_BTSCAN_SORT", "bdaddr", 
												  globalreg->timestamp.tv_sec);
		btscan->sort_type = btscan_sort_bdaddr;
	} else if (menuitem == btscan->mi_sort_bdname) {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdname, 1);
		btscan->pdata->kpinterface->prefs->SetOpt("PLUGIN_BTSCAN_SORT", "bdname", 
												  globalreg->timestamp.tv_sec);
		btscan->sort_type = btscan_sort_bdname;
	} else if (menuitem == btscan->mi_sort_bdclass) {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_bdclass, 1);
		btscan->pdata->kpinterface->prefs->SetOpt("PLUGIN_BTSCAN_SORT", "bdclass", 
												  globalreg->timestamp.tv_sec);
		btscan->sort_type = btscan_sort_bdclass;
	} else if (menuitem == btscan->mi_sort_firsttime) {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_firsttime, 1);
		btscan->pdata->kpinterface->prefs->SetOpt("PLUGIN_BTSCAN_SORT", "firsttime", 
												  globalreg->timestamp.tv_sec);
		btscan->sort_type = btscan_sort_firsttime;
	} else if (menuitem == btscan->mi_sort_lasttime) {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_lasttime, 1);
		btscan->pdata->kpinterface->prefs->SetOpt("PLUGIN_BTSCAN_SORT", "lasttime", 
												  globalreg->timestamp.tv_sec);
		btscan->sort_type = btscan_sort_lasttime;
	} else if (menuitem == btscan->mi_sort_packets) {
		btscan->menu->SetMenuItemChecked(btscan->mi_sort_packets, 1);
		btscan->pdata->kpinterface->prefs->SetOpt("PLUGIN_BTSCAN_SORT", "packets", 
												  globalreg->timestamp.tv_sec);
		btscan->sort_type = btscan_sort_packets;
	}
}

void BtscanProtoBTSCANDEV(CLIPROTO_CB_PARMS) {
	btscan_data *btscan = (btscan_data *) auxptr;

	if (proto_parsed->size() < btscan->asm_btscandev_num) {
		_MSG("Invalid BTSCANDEV sentence from server", MSGFLAG_INFO);
		return;
	}

	int fnum = 0;

	btscan_network *btn = NULL;

	mac_addr ma;

	ma = mac_addr((*proto_parsed)[fnum++].word);

	if (ma.error) {
		return;
	}

	map<mac_addr, btscan_network *>::iterator bti;
	string tstr;
	unsigned int tuint;

	if ((bti = btscan->btdev_map.find(ma)) == btscan->btdev_map.end()) {
		btn = new btscan_network;
		btn->bd_addr = ma;

		btscan->btdev_map[ma] = btn;

		btscan->btdev_vec.push_back(btn);
	} else {
		btn = bti->second;
	}

	tstr = MungeToPrintable((*proto_parsed)[fnum++].word);
	if (btn->bd_name != "" && btn->bd_name != tstr) {
		// alert on BT dev name change?
	}
	btn->bd_name = tstr;

	tstr = MungeToPrintable((*proto_parsed)[fnum++].word);
	if (btn->bd_class != "" && btn->bd_class != tstr) {
		// Alert on BT dev class change?
	}
	btn->bd_class = tstr;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%u", &tuint) != 1) {
		return;
	}
	btn->first_time = tuint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%u", &tuint) != 1) {
		return;
	}
	btn->last_time = tuint;

	if (sscanf((*proto_parsed)[fnum++].word.c_str(), "%u", &tuint) != 1) {
		return;
	}
	btn->packets = 1;

	// TODO - gps
}

void BtscanCliConfigured(CLICONF_CB_PARMS) {
	btscan_data *btscan = (btscan_data *) auxptr;

	// Wipe the scanlist
	btscan->btdevlist->Clear();

	if (kcli->RegisterProtoHandler("BTSCANDEV", btscan->asm_btscandev_fields,
								   BtscanProtoBTSCANDEV, auxptr) < 0) {
		_MSG("Could not register BTSCANDEV protocol with remote server", MSGFLAG_ERROR);

		globalreg->panel_interface->RaiseAlert("No BTSCAN protocol",
				"The BTSCAN UI was unable to register the required\n"
				"BTSCANDEV protocol.  Either it is unavailable\n"
				"(you didn't load the BTSCAN server plugin) or you\n"
				"are using an older server plugin.\n");
		return;
	}
}

void BtscanCliAdd(KPI_ADDCLI_CB_PARMS) {
	if (add == 0)
		return;

	netcli->AddConfCallback(BtscanCliConfigured, 1, auxptr);
}

int BtscanTimer(TIMEEVENT_PARMS) {
	btscan_data *btscan = (btscan_data *) parm;

	// This isn't efficient at all.. but pull the current line, re-sort the 
	// data vector, clear the display, recreate the strings in the table, 
	// re-insert them, and reset the position to the stored one

	vector<string> current_row = btscan->btdevlist->GetSelectedData();

	mac_addr current_bdaddr;

	if (current_row.size() >= 1) 
		current_bdaddr = mac_addr(current_row[0]);

	vector<string> add_row;

	switch (btscan->sort_type) {
		case btscan_sort_bdaddr:
			stable_sort(btscan->btdev_vec.begin(), btscan->btdev_vec.end(),
						Btscan_Sort_Bdaddr());
			break;
		case btscan_sort_bdname:
			stable_sort(btscan->btdev_vec.begin(), btscan->btdev_vec.end(),
						Btscan_Sort_Name());
			break;
		case btscan_sort_bdclass:
			stable_sort(btscan->btdev_vec.begin(), btscan->btdev_vec.end(),
						Btscan_Sort_Class());
			break;
		case btscan_sort_firsttime:
			stable_sort(btscan->btdev_vec.begin(), btscan->btdev_vec.end(),
						Btscan_Sort_Firsttime());
			break;
		case btscan_sort_lasttime:
			stable_sort(btscan->btdev_vec.begin(), btscan->btdev_vec.end(),
						Btscan_Sort_Lasttime());
			break;
		case btscan_sort_packets:
			stable_sort(btscan->btdev_vec.begin(), btscan->btdev_vec.end(),
						Btscan_Sort_Packets());
			break;
		default:
			break;
	}

	btscan->btdevlist->Clear();

	for (unsigned int x = 0; x < btscan->btdev_vec.size(); x++) {
		add_row.clear();

		add_row.push_back(btscan->btdev_vec[x]->bd_addr.Mac2String());
		add_row.push_back(btscan->btdev_vec[x]->bd_name);
		add_row.push_back(btscan->btdev_vec[x]->bd_class);

		btscan->btdevlist->AddRow(x, add_row);

		if (btscan->btdev_vec[x]->bd_addr == current_bdaddr)
			btscan->btdevlist->SetSelected(x);
	}

	return 1;
}
