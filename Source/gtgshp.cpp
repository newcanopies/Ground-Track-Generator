#include <stdio.h>
#include <math.h>
#include <string.h>

#include "Observer.h"
#include "CoordTopographic.h"

#include "gtgshp.h"
#include "gtgutil.h"

/* used to parse attributes specified on command line and as dbf field titles */
enum attribute_ids {
	ATTR_ALTITUDE = 0,
	ATTR_VELOCITY,
	ATTR_TIMEUTC,
	ATTR_TIMEUNIX,
	ATTR_LATITUDE,
	ATTR_LONGITUDE,
	
	ATTR_OBS_FIRST,
	ATTR_OBS_RANGE = ATTR_OBS_FIRST,
	ATTR_OBS_RATE,
	ATTR_OBS_ELEVATION,
	ATTR_OBS_AZIMUTH,
	ATTR_OBS_LAST = ATTR_OBS_AZIMUTH,
	
	ATTR_COUNT
};



/* DBF width and decimal precision values are presently somewhat arbitrary */
struct attribute_options {
	const char *name;
	DBFFieldType type;
	int width;
	int decimals;
} attribute_options[] = {
		{"altitude", FTDouble, 20, 6},  // geodetic alt of sat (km)
		{"velocity", FTDouble, 20, 6},  // magnitude of sat velocity (km/s)
		{"time", FTString, 31, 0},      // YYYY-MM-DD HH:MM:SS.SSSSSS UTC
		{"unixtime", FTInteger, 20, 0}, // unix time (integer seconds)
		{"latitude", FTDouble, 20, 6},  // geodetic lat of sat
		{"longitude", FTDouble, 20, 6}, // geodetic lon of sat
		
		{"range", FTDouble, 30, 6},     // range (km) to observer
		{"rate", FTDouble, 20, 6},      // range rate (km/s) to observer
		{"elevation", FTDouble, 20, 6}, // elevation of sat from obs station
		{"azimuth", FTDouble, 20, 6}    // azimuth of sat from obs station
};

/* each element is set to true if the corresponding attribute should be output */
bool attribute_flags[ATTR_COUNT];

/* the index of the corresponding field in the output attribute table */
int attribute_field[ATTR_COUNT];

/* optional observation station */
bool observer = false;
Observer obs(0, 0, 0);

void SetAttributeObserver(double latitude, double longitude, double altitude)
{
	obs = Observer(latitude, longitude, altitude);
	observer = true;
}

void CheckAttributeObserver(void)
{
	if (not observer) {
		for (int attr = ATTR_OBS_FIRST; attr <= ATTR_OBS_LAST; attr++) {
			if (attribute_flags[attr]) {
				Fail("%s attribute requires an --observer\n", attribute_options[attr].name);
			}
		}
	}
}

void FlagAllAttributes(bool flag_value, bool except_observer_attributes)
{
	for (int attr = 0; attr < ATTR_COUNT; attr++) {
		if (except_observer_attributes &&
				(attr >= ATTR_OBS_FIRST && attr <= ATTR_OBS_LAST)) {
			attribute_flags[attr] = false;
		} else {
			attribute_flags[attr] = flag_value;
		}
	}
}

/* returns index of attribute, if valid, or -1 if not */
int IsValidAttribute(const char *s)
{
	for (int i = 0; i < ATTR_COUNT; i++) {
		if (0 == strcmp(s, attribute_options[i].name)) {
			return i;
		}
	}
	return -1;
}

/* returns true if attribute was enabled; false if not (invalid name) */
bool EnableAttribute(const char *desc)
{
	int attrid = -1;
	if (-1 != (attrid = IsValidAttribute(desc))) {
		attribute_flags[attrid] = true;
		return true;
	}
	return false;
}

void ShapefileWriter::initAttributes(void)
{
	int field;
	for (int attr = 0; attr < ATTR_COUNT; attr++) {
		if (attribute_flags[attr]) {
			field = DBFAddField(dbf_, attribute_options[attr].name, 
					attribute_options[attr].type, attribute_options[attr].width,
					attribute_options[attr].decimals);
			if (-1 == field) {
				Fail("cannot create attribute field: %s\n", attribute_options[attr].name);
			}
			attribute_field[attr] = field;
		}
	}
}

void ShapefileWriter::outputAttributes(int index, Eci *loc, CoordGeodetic *geo)
{
	if (attribute_flags[ATTR_ALTITUDE]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_ALTITUDE],
				geo->altitude);
	}
	
	if (attribute_flags[ATTR_VELOCITY]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_VELOCITY],
				loc->GetVelocity().GetMagnitude());
	}
	
	if (attribute_flags[ATTR_TIMEUTC]) {
		DBFWriteStringAttribute(dbf_, index, attribute_field[ATTR_TIMEUTC],
				loc->GetDate().ToString().c_str());
	}
	
	if (attribute_flags[ATTR_TIMEUNIX]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_TIMEUNIX],
				(double)(loc->GetDate().ToTime()));
	}
		
	if (attribute_flags[ATTR_LATITUDE]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_LATITUDE],
				Util::RadiansToDegrees(geo->latitude));
	}

	if (attribute_flags[ATTR_LONGITUDE]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_LONGITUDE],
				Util::RadiansToDegrees(geo->longitude));
	}
	
	if (attribute_flags[ATTR_OBS_RANGE]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_OBS_RANGE],
				obs.GetLookAngle(*loc).range);
	}
	
	if (attribute_flags[ATTR_OBS_RATE]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_OBS_RATE],
				obs.GetLookAngle(*loc).range_rate);
	}
	
	if (attribute_flags[ATTR_OBS_ELEVATION]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_OBS_ELEVATION],
				Util::RadiansToDegrees(obs.GetLookAngle(*loc).elevation));
	}
	
	if (attribute_flags[ATTR_OBS_AZIMUTH]) {
		DBFWriteDoubleAttribute(dbf_, index, attribute_field[ATTR_OBS_AZIMUTH],
				Util::RadiansToDegrees(obs.GetLookAngle(*loc).azimuth));
	}
}

ShapefileWriter::ShapefileWriter(const char *basepath, enum output_feature_type features)
{
	switch (features) {
		case point:
			shpFormat_ = SHPT_POINT;
			break;
		case line:
			shpFormat_ = SHPT_ARC;
			break;
	}
	
	/* create the shapefile geometry */
	shp_ = SHPCreate(basepath, shpFormat_);
	if (NULL == shp_) {
		Fail("cannot create shapefile: %s\n", basepath);
	}
	
	/* create the shapefile attribute table */
	dbf_ = DBFCreate(basepath);
	if (NULL == dbf_) {
		Fail("cannot create shapefile attribute table: %s\n", basepath);
	}
	
	initAttributes();
}

int ShapefileWriter::output(Eci *loc, Eci *nextloc)
{
	CoordGeodetic locg(loc->ToGeodetic());
	double latitude[2];
	double longitude[2];
	SHPObject *obj = NULL;
	int index;
	int pointc = 1;
	
	/* loc is used for points and line segment start */
	latitude[0] = Util::RadiansToDegrees(locg.latitude);
	longitude[0] = Util::RadiansToDegrees(locg.longitude);
	
	/* nextloc is used for line segment end, if needed */
	if (NULL != nextloc && shpFormat_ == SHPT_ARC) {
		/* not necessary to keep nextlocg around; we use loc for all attributes */
		CoordGeodetic nextlocg(nextloc->ToGeodetic());
		pointc = 2;
		latitude[1] = Util::RadiansToDegrees(nextlocg.latitude);
		longitude[1] = Util::RadiansToDegrees(nextlocg.longitude);

		/* This line segment's endpoints are in different E/W hemispheres */		
		if (cfg.split && ((longitude[0] > 0 && longitude[1] < 0) || (longitude[0] < 0 && longitude[1] > 0))) {
			
			// derived from http://geospatialmethods.org/spheres/
			// assumes spherical earth
			double EARTH_RADIUS = 6367.435; // km
			
			// cartesian coordinates of satellite points
			double radlon0 = Util::DegreesToRadians(longitude[0]);
			double radlat0 = Util::DegreesToRadians(latitude[0]);
			double radlon1 = Util::DegreesToRadians(longitude[1]);
			double radlat1 = Util::DegreesToRadians(latitude[1]);
			double x0 = cos(radlon0) * cos(radlat0);
			double y0 = sin(radlon0) * cos(radlat0);
			double z0 = sin(radlat0);
			double x1 = cos(radlon1) * cos(radlat1);
			double y1 = sin(radlon1) * cos(radlat1);
			double z1 = sin(radlat1);
			
			// coefficients of great circle plane defined by satellite points
			double a1 = (y0 * z1) - (y1 * z0);
			double c1 = (x0 * y1) - (x1 * y0);
						
			// cartesian coordinates g, h, w for one point where that great
			// circle plane intersects the plane of the prime/180th meridian
			double h = -c1 / a1;
			double w = sqrt(pow(EARTH_RADIUS, 2) / (pow(h, 2) + 1));
			
			// spherical coordinates of intersection points
			double lat1 = Util::RadiansToDegrees(asin(w / EARTH_RADIUS));
			double lon1 = (h * w) < 0 ? 180.0 : 0.0;
			double lat2 = Util::RadiansToDegrees(asin(-w / EARTH_RADIUS));
			double lon2 = (-h * w) < 0 ? 180.0 : 0.0;
					
			// negative range_rate indicates satellite is approaching observer;
			// the point that it is approaching is the point it will cross.
			double intercept = 999; // not a valid latitude we'll encounter
			if (Observer(lat1, lon1, 0).GetLookAngle(*loc).range_rate < 0) {
				if (180.0 == lon1) {
					intercept = lat1;
				}
			} else {
				if (180.0 == lon2) {
					intercept = lat2;
				}
			}
			
			// so now after all that we know whether this segment crosses the
			// 180th meridian. If so, split the segment into two pieces.
			// intercept is [APPROXIMATELY] the latitude at which the great
			// circle arc between loc and nextloc crosses the 180th meridian.
			if (999 != intercept) {
				int parts[2];
				double xv[4];
				double yv[4];
				
				parts[0] = 0;
				xv[0] = longitude[0];
				yv[0] = latitude[0];
				xv[1] = longitude[0] < 0 ? -180 : 180;
				yv[1] = intercept;
				
				parts[1] = 2;
				xv[2] = longitude[0] < 0 ? 180 : -180;
				yv[2] = intercept;
				xv[3] = longitude[1];
				yv[3] = latitude[1];
				
				if (NULL == (obj = SHPCreateObject(SHPT_ARC, -1, 2, parts, NULL, 4, xv, yv, NULL, NULL))) {
					Fail("cannot create split line segment\n");
				}
				Note("Split segment at dateline at latitude: %lf\n", intercept);
			}			
		}
	} else if (shpFormat_ == SHPT_ARC) {
		Fail("line output requires two points; only one received\n");
	}
		
	/* output the geometry */
	if (NULL == obj) {
		// in most cases we'll need to do this, but if we crossed a key meridian
		// we'll already have a split shape object to output
		obj = SHPCreateSimpleObject(shpFormat_, pointc, longitude, latitude, NULL);
	}
	if (NULL == obj) {
		Fail("cannot create shape\n"); // not very informative
	}
	index = SHPWriteObject(shp_, -1, obj);
	SHPDestroyObject(obj);
	
	outputAttributes(index, loc, &locg);

	Note("Lat: %lf, Lon: %lf\n", latitude[0], longitude[0]);
	
	return index;
}

void ShapefileWriter::close(void)
{
	SHPClose(shp_);
	DBFClose(dbf_);
}
