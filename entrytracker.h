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

#ifndef __ENTRYTRACKER_H__
#define __ENTRYTRACKER_H__

#include "config.h"

#include <stdio.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <map>

#include "globalregistry.h"
#include "kis_mutex.h"
#include "trackedelement.h"
#include "kis_net_microhttpd.h"

// Allocate and track named fields and give each one a custom int
class EntryTracker : public Kis_Net_Httpd_CPPStream_Handler, public LifetimeGlobal {
public:
    static std::shared_ptr<EntryTracker> create_entrytracker(GlobalRegistry *in_globalreg) {
        std::shared_ptr<EntryTracker> mon(new EntryTracker(in_globalreg));
        in_globalreg->entrytracker = mon.get();
        in_globalreg->RegisterLifetimeGlobal(mon);
        in_globalreg->InsertGlobal("ENTRY_TRACKER", mon);
        return mon;
    }

private:
    EntryTracker(GlobalRegistry *in_globalreg);

public:
    virtual ~EntryTracker();

    // Reserve a field name.  Field names are plain strings, which can
    // be used to derive namespaces later.
    // Return: Registered field number, or negative on error (such as field exists with
    // conflicting type)
    int RegisterField(std::string in_name, TrackerType in_type, std::string in_desc);

    // Reserve a field name, and return an instance.  If the field ALREADY EXISTS, return
    // an instance.
    std::shared_ptr<TrackerElement> RegisterAndGetField(std::string in_name, 
            TrackerType in_type, std::string in_desc);

    // Reserve a field name, include a builder instance of the field instead of a 
    // fixed type.  Used for building complex types w/ storage backed by trackable 
    // elements.
    // Returns: Registered field number, or negative on error (such as field exists with a 
    // static type.  No checking can be performed between two fields generated by builder
    // instances.)
    int RegisterField(std::string in_name, std::shared_ptr<TrackerElement> in_builder, 
            std::string in_desc);

    // Reserve a field name, and return an instance.  If the field ALREADY EXISTS, return
    // an instance.
    std::shared_ptr<TrackerElement> RegisterAndGetField(std::string in_name, 
            std::shared_ptr<TrackerElement> in_builder, std::string in_desc);

    int GetFieldId(std::string in_name);
    std::string GetFieldName(int in_id);
    std::string GetFieldDescription(int in_id);
    TrackerType GetFieldType(int in_id);

    // Get a field instance
    // Return: NULL if unknown
    std::shared_ptr<TrackerElement> GetTrackedInstance(std::string in_name);
    std::shared_ptr<TrackerElement> GetTrackedInstance(int in_id);

    // Register a serializer for auto-serialization based on type
    void RegisterSerializer(std::string type, std::shared_ptr<TrackerElementSerializer> in_ser);
    void RemoveSerializer(std::string type);
    bool CanSerialize(std::string type);
    bool Serialize(std::string type, std::ostream &stream, SharedTrackerElement elem,
            TrackerElementSerializer::rename_map *name_map = NULL);

    // HTTP api
    virtual bool Httpd_VerifyPath(const char *path, const char *method);

    virtual void Httpd_CreateStreamResponse(Kis_Net_Httpd *httpd,
            Kis_Net_Httpd_Connection *connection,
            const char *url, const char *method, const char *upload_data,
            size_t *upload_data_size, std::stringstream &stream);

protected:
    GlobalRegistry *globalreg;

    kis_recursive_timed_mutex entry_mutex;
    kis_recursive_timed_mutex serializer_mutex;

    int next_field_num;

    struct reserved_field {
        // ID we assigned
        int field_id;

        // How we create it
        std::string field_name;
        TrackerType track_type;

        // Or a builder instance
        std::shared_ptr<TrackerElement> builder;

        // Might as well track this for auto-doc
        std::string field_description;
    };

    std::map<std::string, std::shared_ptr<reserved_field> > field_name_map;
    typedef std::map<std::string, std::shared_ptr<reserved_field> >::iterator name_itr;

    std::map<int, std::shared_ptr<reserved_field> > field_id_map;
    typedef std::map<int, std::shared_ptr<reserved_field> >::iterator id_itr;

    std::map<std::string, std::shared_ptr<TrackerElementSerializer> > serializer_map;
    typedef std::map<std::string, std::shared_ptr<TrackerElementSerializer> >::iterator serial_itr;

};

class SerializerScope {
public:
    SerializerScope(SharedTrackerElement e, TrackerElementSerializer::rename_map *name_map) {
        elem = e;
        rnmap = name_map;

        if (rnmap != NULL) {
            auto nmi = rnmap->find(elem);
            if (nmi != rnmap->end()) {
                TrackerElementSerializer::pre_serialize_path(nmi->second);
            } else {
                elem->pre_serialize();
            } 
        } else {
            elem->pre_serialize();
        }
    }

    virtual ~SerializerScope() {
        if (rnmap != NULL) {
            auto nmi = rnmap->find(elem);
            if (nmi != rnmap->end()) {
                TrackerElementSerializer::post_serialize_path(nmi->second);
            } else {
                elem->post_serialize();
            } 
        } else {
            elem->post_serialize();
        }

    }

protected:
    SharedTrackerElement elem;
    TrackerElementSerializer::rename_map *rnmap;
};

#endif
