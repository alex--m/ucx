#!/usr/bin/python

"""
This python script parses the binary result files of the instrumentation
capability. Each file is composed of a header and a variable number of
records containing a timestamp and some custom recorded content.
"""

import sys
import struct

HEADER_FORMAT = "1024sIL1024si40sLLLQQ"
HEADER_FIELDS = [
    "library path",
    "checksum",
    "library loading base",
    "command line",
    "process id",
    "host name",
    "record count",
    "location count",
    "record offset",
    "start time",
    "one second in units"
] 

LOCATION_FORMAT = "II256s"
LOCATION_FIELDS = [
    "location",
    "extra",
    "name"
]

RECORD_FORMAT = "LLII"
RECORD_FIELDS = [
    "timestamp",
    "lparam",
    "wparam",
    "location"
]

LOCATION_INTERVALS = {

    "INSTRUMENT_TYPE_IB_TX_uct_rc_verbs_ep_post_send" :
        "INSTRUMENT_TYPE_IB_TX_uct_rc_verbs_iface_poll_tx",

    "INSTRUMENT_TYPE_IB_TX_uct_rc_verbs_ep_exp_post_send" :
        "INSTRUMENT_TYPE_IB_TX_uct_rc_verbs_iface_poll_tx",

    "INSTRUMENT_TYPE_IB_TX_uct_ud_verbs_ep_tx_skb" :
        "INSTRUMENT_TYPE_IB_TX_uct_ud_verbs_ep_tx_skb"
"""
    "INSTRUMENT_TYPE_IB_RX_uct_ud_verbs_iface_post_recv_always" :
        "INSTRUMENT_TYPE_IB_RX_uct_ud_verbs_iface_poll_rx",

    "INSTRUMENT_TYPE_IB_RX_uct_rc_verbs_iface_post_recv_always" :
        "INSTRUMENT_TYPE_IB_RX_uct_rc_verbs_iface_poll_rx",
"""
}

SECOND_IN_UNITS = 1 # Default value

def read_instrumentation_file(file_path):
    print "\n\n%s :\n" % file_path
    with open(file_path, "rb") as f:
        # Read instrumentation header
        raw_header = f.read(struct.calcsize(HEADER_FORMAT))
        header = dict(zip(HEADER_FIELDS,
            struct.unpack(HEADER_FORMAT, raw_header)))
        for k,v in header.iteritems():
            print "%-20s : %s" % (k, str(v).replace("\x00",""))

        global SECOND_IN_UNITS
        SECOND_IN_UNITS = header["one second in units"]

        locations = {}
        for i in range(header["location count"]):
            raw_location = f.read(struct.calcsize(LOCATION_FORMAT))
            location = dict(zip(LOCATION_FIELDS,
                struct.unpack(LOCATION_FORMAT, raw_location)))
            locations[location["location"]] = location["name"].replace("\x00","")
        for k,v in locations.iteritems():
            print "%-20i : %s" % (k, v)

        for record in range(header["record count"]):
            # Read a single instrumentation record
            raw_record = f.read(struct.calcsize(RECORD_FORMAT))
            record = dict(zip(RECORD_FIELDS,
                struct.unpack(RECORD_FORMAT, raw_record)))

            if record["location"] in locations:
                location = locations[record["location"]]
            else:
                print "ERROR: Unknown location %s !" % record["location"]
                location = "Unknown (%s)" % record["location"]

            print "Timestamp: %u\tLocation: %s\tWR ID: %u\tLength: %u" % \
                (record["timestamp"], location, record["lparam"], record["wparam"])

            yield record, location

def timestamp_analysis(file_path):
    locations = {}
    work_requests = {}
    for record, location in read_instrumentation_file(file_path):
        if location not in locations:
            locations[location] = { "count" : 0, "sum" : 0 }

        wr_id = record["lparam"]
        if wr_id in work_requests:
            time_interval =  record["timestamp"] - \
                work_requests[wr_id]["timestamp"]
            original_location = work_requests[wr_id]["location"]

            if LOCATION_INTERVALS[original_location] != location:
                print "ERROR: Work request #%s started at %s but finished at %s !" % \
                    (wr_id, original_location, location)
            locations[original_location]["sum"] += time_interval
            locations[original_location]["count"] += 1
            del work_requests[wr_id]

        else:
            if location in LOCATION_INTERVALS:
                work_requests[wr_id] = { "location": location, 
                    "timestamp":record["timestamp"] }
            else:
                print "ERROR: Work request started with %s !" % location

    # Output statistics
    for location, stats in locations.iteritems():
        if stats["count"]:
            avg = ((0.0 + stats["sum"]) / stats["count"])
            print "location=%s count=%i average=%f microseconds" % (location,
                stats["count"], 10 ** 6 * avg / SECOND_IN_UNITS)
        print SECOND_IN_UNITS, " SECOND_IN_UNITS"

if __name__ == "__main__":
    for file_path in sys.argv[1:]:
        timestamp_analysis(file_path)
