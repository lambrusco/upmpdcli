/* Copyright (C) 2014 J.F.Dockes
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

#include "conman.hxx"

#include <upnp/upnp.h>

#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "libupnpp/log.hxx"
#include "libupnpp/soaphelp.hxx"

using namespace std;
using namespace std::placeholders;
using namespace UPnPP;
using namespace UPnPProvider;

static const string sTpCM("urn:schemas-upnp-org:service:ConnectionManager:1");
static const string sIdCM("urn:upnp-org:serviceId:ConnectionManager");

UpMpdConMan::UpMpdConMan(UpnpDevice *dev, const string& protoinfo)
    : UpnpService(sTpCM, sIdCM, dev), m_protoinfo(protoinfo)
{
    dev->addActionMapping(this,"GetCurrentConnectionIDs", 
                          bind(&UpMpdConMan::getCurrentConnectionIDs, 
                               this, _1,_2));
    dev->addActionMapping(this,"GetCurrentConnectionInfo", 
                          bind(&UpMpdConMan::getCurrentConnectionInfo, 
                               this,_1,_2));
    dev->addActionMapping(this,"GetProtocolInfo", 
                          bind(&UpMpdConMan::getProtocolInfo, this, _1, _2));
}

bool UpMpdConMan::getEventData(bool all, std::vector<std::string>& names, 
                                 std::vector<std::string>& values)
{
    //LOGDEB("UpMpd:getEventDataCM" << endl);

    // Our data never changes, so if this is not an unconditional request,
    // we return nothing.
    if (all) {
        names.push_back("SinkProtocolInfo");
        values.push_back(m_protoinfo);
    }
    return true;
}

int UpMpdConMan::getCurrentConnectionIDs(const SoapIncoming& sc, SoapOutgoing& data)
{
    LOGDEB("UpMpd:getCurrentConnectionIDs" << endl);
    data.addarg("ConnectionIDs", "0");
    return UPNP_E_SUCCESS;
}

int UpMpdConMan::getCurrentConnectionInfo(const SoapIncoming& sc, SoapOutgoing& data)
{
    LOGDEB("UpMpdConMan:getCurrentConnectionInfo" << endl);

    string conid;
    if (!sc.get("ConnectionID", &conid) || conid.compare("0")) {
        return UPNP_E_INVALID_PARAM;
    }

    data.addarg("RcsID", "0");
    data.addarg("AVTransportID", "0");
    data.addarg("ProtocolInfo", "");
    data.addarg("PeerConnectionManager", "");
    data.addarg("PeerConnectionID", "-1");
    data.addarg("Direction", "Input");
    data.addarg("Status", "Unknown");

    return UPNP_E_SUCCESS;
}

int UpMpdConMan::getProtocolInfo(const SoapIncoming& sc, SoapOutgoing& data)
{
    LOGDEB("UpMpdConMan::getProtocolInfo" << endl);
    data.addarg("Source", "");
    data.addarg("Sink", m_protoinfo);

    return UPNP_E_SUCCESS;
}
