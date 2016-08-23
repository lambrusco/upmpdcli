/* Copyright (C) 2016 J.F.Dockes
 *	 This program is free software; you can redistribute it and/or modify
 *	 it under the terms of the GNU General Public License as published by
 *	 the Free Software Foundation; either version 2 of the License, or
 *	 (at your option) any later version.
 *
 *	 This program is distributed in the hope that it will be useful,
 *	 but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	 GNU General Public License for more details.
 *
 *	 You should have received a copy of the GNU General Public License
 *	 along with this program; if not, write to the
 *	 Free Software Foundation, Inc.,
 *	 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#define LOGGER_LOCAL_LOGINC 3

#include "contentdirectory.hxx"

#include <upnp/upnp.h>

#include <functional>
#include <iostream>
#include <map>
#include <utility>
#include <unordered_map>
#include <sstream>

#include "libupnpp/log.hxx"
#include "libupnpp/soaphelp.hxx"
#include "libupnpp/device/device.hxx"

#include "pathut.h"
#include "smallut.h"
#include "upmpdutils.hxx"
#include "main.hxx"
#include "cdplugins/cdplugin.hxx"
#include "cdplugins/tidal.hxx"

using namespace std;
using namespace std::placeholders;
using namespace UPnPProvider;

class ContentDirectory::Internal {
public:
    Internal (ContentDirectory *sv)
	: service(sv) {
    }
    ~Internal() {
	for (auto& it : plugins) {
	    delete it.second;
	}
    }
    CDPlugin *pluginFactory(const string& appname) {
	LOGDEB("ContentDirectory::pluginFactory: for " << appname << endl);

	if (httphp.empty()) {
	    UpnpDevice *dev;
	    if (!service || !(dev = service->getDevice())) {
		LOGERR("ContentDirectory::Internal: no service or dev ??\n");
		return nullptr;
	    }
	    string host;
	    unsigned short port;
	    if (!dev->ipv4(&host, &port)) {
		LOGERR("ContentDirectory::Internal: can't get server IP\n");
		return nullptr;
	    }
	    ostringstream ss;
	    ss << host << ":" << port;
	    httphp = ss.str();
	    LOGDEB("ContentDirectory: host:port: " << httphp << endl);
	}
	VirtualDir *dir = VirtualDir::getVirtualDir();
	if (dir == nullptr) {
	    LOGERR("CDPlugin::factory: getVirtualDir() failed\n");
	    return nullptr;
	}
	if (!appname.compare("tidal")) {
	    string pthcdplugs = path_cat(g_datadir, "cdplugins");
	    Tidal *tidal =
		new Tidal({pthcdplugs, path_cat(pthcdplugs, appname)},
			  httphp, "/tidal");
	    if (tidal) {
		dir->addVDir("/tidal", tidal->getFileOps());
	    }
	    return tidal;
	} else {
	    return nullptr;
	}
    }
    CDPlugin *pluginForApp(const string& appname) {
	auto it = plugins.find(appname);
	if (it != plugins.end()) {
	    return it->second;
	} else {
	    CDPlugin *plug = pluginFactory(appname);
	    if (plug) {
		plugins[appname] = plug;
	    }
	    return plug;
	}
    }
    unordered_map<string, CDPlugin *> plugins;
    ContentDirectory *service;
    string httphp;
};

static const string
sTpContentDirectory("urn:schemas-upnp-org:service:ContentDirectory:1");
static const string
sIdContentDirectory("urn:upnp-org:serviceId:ContentDirectory");

ContentDirectory::ContentDirectory(UPnPProvider::UpnpDevice *dev)
    : UpnpService(sTpContentDirectory, sIdContentDirectory, dev),
      m(new Internal(this))
{
    dev->addActionMapping(
        this, "GetSearchCapabilities",
        bind(&ContentDirectory::actGetSearchCapabilities, this, _1, _2));
    dev->addActionMapping(
        this, "GetSortCapabilities",
        bind(&ContentDirectory::actGetSortCapabilities, this, _1, _2));
    dev->addActionMapping(
        this, "GetSystemUpdateID",
        bind(&ContentDirectory::actGetSystemUpdateID, this, _1, _2));
    dev->addActionMapping(
        this, "Browse",
        bind(&ContentDirectory::actBrowse, this, _1, _2));
    dev->addActionMapping(
        this, "Search",
        bind(&ContentDirectory::actSearch, this, _1, _2));
}

ContentDirectory::~ContentDirectory()
{
    delete m;
}

int ContentDirectory::actGetSearchCapabilities(const SoapIncoming& sc, SoapOutgoing& data)
{
    LOGDEB("ContentDirectory::actGetSearchCapabilities: " << endl);

    std::string out_SearchCaps = "upnp:artist,dc:creator,upnp:album,dc:title";
    data.addarg("SearchCaps", out_SearchCaps);
    return UPNP_E_SUCCESS;
}

int ContentDirectory::actGetSortCapabilities(const SoapIncoming& sc, SoapOutgoing& data)
{
    LOGDEB("ContentDirectory::actGetSortCapabilities: " << endl);

    std::string out_SortCaps;
    data.addarg("SortCaps", out_SortCaps);
    return UPNP_E_SUCCESS;
}

int ContentDirectory::actGetSystemUpdateID(const SoapIncoming& sc, SoapOutgoing& data)
{
    LOGDEB("ContentDirectory::actGetSystemUpdateID: " << endl);

    std::string out_Id;
    data.addarg("Id", out_Id);
    return UPNP_E_SUCCESS;
}

static vector<UpSong> rootdir;
void makerootdir()
{
    rootdir.push_back(UpSong::container("0$tidal$", "0", "Tidal"));
}

// Returns totalmatches
static size_t readroot(int offs, int cnt, vector<UpSong>& out)
{
    //LOGDEB("readroot: offs " << offs << " cnt " << cnt << endl);
    if (rootdir.empty()) {
	makerootdir();
    }
    out.clear();
    if (offs < 0 || cnt <= 0) {
	return rootdir.size();
    }
	
    for (int i = 0; i < cnt; i++) {
	if (size_t(offs + i) < rootdir.size()) {
	    out.push_back(rootdir[offs + i]);
	} else {
	    break;
	}
    }
    //LOGDEB("readroot: returning " << out.size() << " entries\n");
    return rootdir.size();
}

static string appForId(const string& id)
{
    string app;
    string::size_type dol0 = id.find_first_of("$");
    if (dol0 == string::npos) {
	LOGERR("ContentDirectory::appForId: bad id [" << id << "]\n");
	return string();
    } 
    string::size_type dol1 = id.find_first_of("$", dol0 + 1);
    if (dol1 == string::npos) {
	LOGERR("ContentDirectory::appForId: bad id [" << id << "]\n");
	return string();
    } 
    return id.substr(dol0 + 1, dol1 - dol0 -1);
}

int ContentDirectory::actBrowse(const SoapIncoming& sc, SoapOutgoing& data)
{
    bool ok = false;
    std::string in_ObjectID;
    ok = sc.get("ObjectID", &in_ObjectID);
    if (!ok) {
        LOGERR("ContentDirectory::actBrowse: no ObjectID in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    std::string in_BrowseFlag;
    ok = sc.get("BrowseFlag", &in_BrowseFlag);
    if (!ok) {
        LOGERR("ContentDirectory::actBrowse: no BrowseFlag in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    std::string in_Filter;
    ok = sc.get("Filter", &in_Filter);
    if (!ok) {
        LOGERR("ContentDirectory::actBrowse: no Filter in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    int in_StartingIndex;
    ok = sc.get("StartingIndex", &in_StartingIndex);
    if (!ok) {
        LOGERR("ContentDirectory::actBrowse: no StartingIndex in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    int in_RequestedCount;
    ok = sc.get("RequestedCount", &in_RequestedCount);
    if (!ok) {
        LOGERR("ContentDirectory::actBrowse: no RequestedCount in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    std::string in_SortCriteria;
    ok = sc.get("SortCriteria", &in_SortCriteria);
    if (!ok) {
        LOGERR("ContentDirectory::actBrowse: no SortCriteria in params\n");
        return UPNP_E_INVALID_PARAM;
    }

    LOGDEB("ContentDirectory::actBrowse: " << " ObjectID " << in_ObjectID <<
	   " BrowseFlag " << in_BrowseFlag << " Filter " << in_Filter <<
	   " StartingIndex " << in_StartingIndex <<
	   " RequestedCount " << in_RequestedCount <<
	   " SortCriteria " << in_SortCriteria << endl);

    vector<string> sortcrits;
    stringToStrings(in_SortCriteria, sortcrits);

    CDPlugin::BrowseFlag bf;
    if (in_BrowseFlag.compare("BrowseMetadata")) {
	bf = CDPlugin::BFMeta;
    } else {
	bf = CDPlugin::BFChildren;
    }
    std::string out_Result;
    std::string out_NumberReturned;
    std::string out_TotalMatches;
    std::string out_UpdateID;

    // Go fetch
    vector<UpSong> entries;
    size_t tot = 0;
    if (!in_ObjectID.compare("0")) {
	// Root directory: we do this ourselves
	tot = readroot(in_StartingIndex, in_RequestedCount, entries);
    } else {
	// Pass off request to appropriate app, defined by 1st elt in id
	string app = appForId(in_ObjectID);
	LOGDEB("ContentDirectory::actBrowse: app: [" << app << "]\n");

	CDPlugin *plg = m->pluginForApp(app);
	if (plg) {
	    out_TotalMatches = plg->browse(in_ObjectID, in_StartingIndex,
					   in_RequestedCount, entries,
					   sortcrits, bf);
	} else {
	    LOGERR("ContentDirectory::Browse: unknown app: [" << app << "]\n");
	}
    }


    // Process and send out result
    out_NumberReturned = ulltodecstr(entries.size());
    out_TotalMatches = ulltodecstr(tot);
    out_UpdateID = "1";
    out_Result = headDIDL();
    for (unsigned int i = 0; i < entries.size(); i++) {
	out_Result += entries[i].didl();
    } 
    out_Result += tailDIDL();
    
    data.addarg("Result", out_Result);
    LOGDEB("ContentDirectory::actBrowse: result [" << out_Result << "]\n");
    data.addarg("NumberReturned", out_NumberReturned);
    data.addarg("TotalMatches", out_TotalMatches);
    data.addarg("UpdateID", out_UpdateID);
    return UPNP_E_SUCCESS;
}

int ContentDirectory::actSearch(const SoapIncoming& sc, SoapOutgoing& data)
{
    bool ok = false;
    std::string in_ContainerID;
    ok = sc.get("ContainerID", &in_ContainerID);
    if (!ok) {
        LOGERR("ContentDirectory::actSearch: no ContainerID in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    std::string in_SearchCriteria;
    ok = sc.get("SearchCriteria", &in_SearchCriteria);
    if (!ok) {
        LOGERR("ContentDirectory::actSearch: no SearchCriteria in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    std::string in_Filter;
    ok = sc.get("Filter", &in_Filter);
    if (!ok) {
        LOGERR("ContentDirectory::actSearch: no Filter in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    int in_StartingIndex;
    ok = sc.get("StartingIndex", &in_StartingIndex);
    if (!ok) {
        LOGERR("ContentDirectory::actSearch: no StartingIndex in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    int in_RequestedCount;
    ok = sc.get("RequestedCount", &in_RequestedCount);
    if (!ok) {
        LOGERR("ContentDirectory::actSearch: no RequestedCount in params\n");
        return UPNP_E_INVALID_PARAM;
    }
    std::string in_SortCriteria;
    ok = sc.get("SortCriteria", &in_SortCriteria);
    if (!ok) {
        LOGERR("ContentDirectory::actSearch: no SortCriteria in params\n");
        return UPNP_E_INVALID_PARAM;
    }

    LOGDEB("ContentDirectory::actSearch: " <<
	   " ContainerID " << in_ContainerID <<
	   " SearchCriteria " << in_SearchCriteria <<
	   " Filter " << in_Filter << " StartingIndex " << in_StartingIndex <<
	   " RequestedCount " << in_RequestedCount <<
	   " SortCriteria " << in_SortCriteria << endl);

    vector<string> sortcrits;
    stringToStrings(in_SortCriteria, sortcrits);

    std::string out_Result;
    std::string out_NumberReturned = "0";
    std::string out_TotalMatches = "0";
    std::string out_UpdateID;

    // Go fetch
    vector<UpSong> entries;
    size_t tot = 0;
    if (!in_ContainerID.compare("0")) {
	// Root directory: can't search in there
	LOGERR("ContentDirectory::actSearch: Can't search in root\n");
    } else {
	// Pass off request to appropriate app, defined by 1st elt in id
	// Pass off request to appropriate app, defined by 1st elt in id
	string app = appForId(in_ContainerID);
	LOGDEB("ContentDirectory::actSearch: app: [" << app << "]\n");

	CDPlugin *plg = m->pluginForApp(app);
	if (plg) {
	    out_TotalMatches = plg->search(in_ContainerID, in_StartingIndex,
					   in_RequestedCount, 
					   in_SearchCriteria,
					   entries, sortcrits);
	} else {
	    LOGERR("ContentDirectory::Browse: unknown app: [" << app << "]\n");
	}
    }


    // Process and send out result
    out_NumberReturned = ulltodecstr(entries.size());
    out_TotalMatches = ulltodecstr(tot);
    out_UpdateID = "1";
    out_Result = headDIDL();
    for (unsigned int i = 0; i < entries.size(); i++) {
	out_Result += entries[i].didl();
    } 
    out_Result += tailDIDL();
    
    data.addarg("Result", out_Result);
    data.addarg("NumberReturned", out_NumberReturned);
    data.addarg("TotalMatches", out_TotalMatches);
    data.addarg("UpdateID", out_UpdateID);
    return UPNP_E_SUCCESS;
}

