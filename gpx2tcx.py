#!/usr/bin/env python3
import sys
import math
import xml.etree.ElementTree as ET
from datetime import datetime, timezone, timedelta

def haversine(lat1, lon1, lat2, lon2):
    R = 6371000.0
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2)**2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2)**2
    return 2 * R * math.atan2(math.sqrt(a), math.sqrt(1 - a))

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} input.gpx output.tcx [max_snap_meters]", file=sys.stderr)
        sys.exit(1)

    in_gpx = sys.argv[1]
    out_tcx = sys.argv[2]
    max_dist = float(sys.argv[3]) if len(sys.argv) > 3 else 250.0

    tree = ET.parse(in_gpx)
    root = tree.getroot()
    ns = {'gpx': 'http://www.topografix.com/GPX/1/1'}

    # Extract track name
    trk_name_el = root.find('.//gpx:trk/gpx:name', ns)
    course_name = trk_name_el.text.strip() if (trk_name_el is not None and trk_name_el.text) else "CalTopo Route"

    # Extract trackpoints
    trkpts = []
    for pt in root.findall('.//gpx:trkpt', ns):
        lat = float(pt.attrib['lat'])
        lon = float(pt.attrib['lon'])
        ele_el = pt.find('gpx:ele', ns)
        ele = float(ele_el.text) if (ele_el is not None and ele_el.text) else 0.0
        trkpts.append({'lat': lat, 'lon': lon, 'ele': ele})

    if not trkpts:
        print("Error: No <trkpt> elements found. Export from CalTopo as a Track, not a Route.", file=sys.stderr)
        sys.exit(1)

    # Calculate cumulative distances and synthetic timestamps (Garmin Connect requires <Time>)
    start_time = datetime(2026, 1, 1, 8, 0, 0, tzinfo=timezone.utc)
    cum_dist = 0.0
    for i, pt in enumerate(trkpts):
        if i > 0:
            cum_dist += haversine(trkpts[i-1]['lat'], trkpts[i-1]['lon'], pt['lat'], pt['lon'])
        pt['dist'] = cum_dist
        # ~1.4 m/s (~3 mph) synthetic pace for valid monotonic timestamps
        pt['time'] = (start_time + timedelta(seconds=int(cum_dist / 1.4))).strftime('%Y-%m-%dT%H:%M:%SZ')

    # Snap waypoints to closest trackpoint
    course_points = []
    for wpt in root.findall('.//gpx:wpt', ns):
        w_lat = float(wpt.attrib['lat'])
        w_lon = float(wpt.attrib['lon'])
        name_el = wpt.find('gpx:name', ns)
        w_name = name_el.text.strip() if (name_el is not None and name_el.text) else "Waypoint"

        best_idx = min(range(len(trkpts)), key=lambda idx: haversine(w_lat, w_lon, trkpts[idx]['lat'], trkpts[idx]['lon']))
        snap_dist = haversine(w_lat, w_lon, trkpts[best_idx]['lat'], trkpts[best_idx]['lon'])

        if snap_dist <= max_dist:
            course_points.append({
                'name': w_name[:15],  # Garmin watches truncate CoursePoint names around 15 chars
                'full_name': w_name,
                'lat': trkpts[best_idx]['lat'],
                'lon': trkpts[best_idx]['lon'],
                'time': trkpts[best_idx]['time'],
                'dist': trkpts[best_idx]['dist'],
                'snap_offset': snap_dist
            })
        else:
            print(f"Skipped '{w_name}': {snap_dist:.1f}m from track (exceeds {max_dist}m limit)", file=sys.stderr)

    # Sort course points chronologically along the track
    course_points.sort(key=lambda x: x['dist'])

    # Build TCX XML
    tcx_ns = "http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2"
    ET.register_namespace('', tcx_ns)
    tcx_root = ET.Element(f"{{{tcx_ns}}}TrainingCenterDatabase")
    courses = ET.SubElement(tcx_root, "Courses")
    course = ET.SubElement(courses, "Course")
    ET.SubElement(course, "Name").text = course_name[:15]

    track = ET.SubElement(course, "Track")
    for pt in trkpts:
        tp = ET.SubElement(track, "Trackpoint")
        ET.SubElement(tp, "Time").text = pt['time']
        pos = ET.SubElement(tp, "Position")
        ET.SubElement(pos, "LatitudeDegrees").text = f"{pt['lat']:.7f}"
        ET.SubElement(pos, "LongitudeDegrees").text = f"{pt['lon']:.7f}"
        ET.SubElement(tp, "AltitudeMeters").text = f"{pt['ele']:.1f}"
        ET.SubElement(tp, "DistanceMeters").text = f"{pt['dist']:.1f}"

    for cp in course_points:
        cp_el = ET.SubElement(course, "CoursePoint")
        ET.SubElement(cp_el, "Name").text = cp['name']
        ET.SubElement(cp_el, "Time").text = cp['time']
        pos = ET.SubElement(cp_el, "Position")
        ET.SubElement(pos, "LatitudeDegrees").text = f"{cp['lat']:.7f}"
        ET.SubElement(pos, "LongitudeDegrees").text = f"{cp['lon']:.7f}"
        ET.SubElement(cp_el, "PointType").text = "Generic"
        print(f"Added CoursePoint '{cp['name']}' (from '{cp['full_name']}') at {cp['dist']/1000:.2f} km (snapped {cp['snap_offset']:.1f}m)")

    ET.indent(tcx_root, space="  ")
    ET.ElementTree(tcx_root).write(out_tcx, encoding="utf-8", xml_declaration=True)

if __name__ == "__main__":
    main()
