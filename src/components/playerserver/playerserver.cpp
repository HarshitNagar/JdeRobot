/*
 *
 *  Copyright (C) 1997-2010 JDERobot Developers Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see http://www.gnu.org/licenses/. 
 *
 *  Authors : Julio Vega  <julio.vega@urjc.es>
 *						Eduardo Perdices  <eperdices@gsyc.es>
 *
 */

// ICE utils includes
#include <Ice/Ice.h>
#include <IceUtil/IceUtil.h>
#include <gbxsickacfr/gbxiceutilacfr/safethread.h>

// JDErobot general ice component includes
#include <jderobotice/component.h>
#include <jderobotice/application.h>
#include <tr1/memory>
#include <list>
#include <sstream>
#include <jderobotice/exceptions.h>

// JDErobot specific ice component includes
#include <jderobot/motors.h>
#include <jderobot/laser.h>
#include <jderobot/encoders.h>
#include <jderobot/ptencoders.h>
#include <jderobot/ptmotors.h>
#include <jderobot/sonars.h>

// Library includes
#include <math.h>
#include <libplayerc++/playerc++.h>
#include <libplayerc++/playererror.h>
#include <libplayerc++/utility.h>
#include <libplayerc++/playerc++config.h>
#include <libplayerc++/playerclient.h>
#include <libplayerc++/clientproxy.h>
#include <libplayerc/playerc.h>

// Constants
#define DEGTORAD 0.01745327
#define RADTODEG 57.29582790
#define MIN_PAN 40.0
#define MAX_PAN 160.0
#define MIN_TILT 40.0
#define MAX_TILT 120.0
#define ITERATION_TIME 75 //ms

/*Laser configuration*/
#define NUM_LASER_DEF 180
#define RESOLUTION_LASER_DEF 1

/*Sonar configuration*/
#define NUM_SONARS_DEF 16

using namespace std;

// Global variables
string playerhost;
int playerport;

namespace playerserver {
	class MotorsI: virtual public jderobot::Motors {
		public:
			MotorsI (std::string& propertyPrefix, const jderobotice::Context& context):
			prefix(propertyPrefix),context(context) {
				Ice::PropertiesPtr prop = context.properties();

				playerserver_id=0;
				playerclient_id=0;

				// Create a client object
				cout << "Connecting to Player Server at '" << playerhost << ":" << playerport << "..." << endl;
				playerclient = playerc_client_create (NULL, playerhost.c_str(), playerport);

				// Connect to the server
				if (playerc_client_connect(playerclient) != 0)	{
					cout << "ERROR: Connecting to Player Server" << endl;
				}

				// Getting player server position
				player_position = playerc_position2d_create (playerclient, 0);  
				if (playerc_position2d_subscribe(player_position, PLAYER_OPEN_MODE) != 0)	{
					player_position = NULL;
					cout << "ERROR: Getting Player Position" << endl;
				}

				// Enable the robots motors
				playerc_position2d_enable(player_position,1);

				// Start the robot
				player_v = 0.0;
				player_w = 0.0;
  			playerc_position2d_set_cmd_vel(player_position, player_v, 0, player_w, 0);
			}

			virtual ~MotorsI(){};

			virtual float getV (const Ice::Current&) {
				return player_v;
			};

			virtual float getW (const Ice::Current&) {
				return player_w;
			};

			virtual Ice::Int setV (Ice::Float v, const Ice::Current&) {
				player_v = v;

				return 0;
			};

			virtual Ice::Int setW (Ice::Float w, const Ice::Current&) {
				player_w = w;

				return 0;
			};

			virtual float getL(const Ice::Current&) {
				return (float)0.0;
			};

			virtual  Ice::Int setL(Ice::Float w, const Ice::Current&) {
				return 0;
			};

			virtual void update() {
				// Update robot vel
				playerc_position2d_set_cmd_vel(player_position, player_v, 0, player_w, 0);	

				// Read data from the server
				if(playerc_client_read(playerclient) == NULL) {
					cout << "ERROR: Reading from Player Server" << endl;
				}			
			};

			std::string prefix;
			jderobotice::Context context;

			playerc_client_t *playerclient;
			int playerserver_id;
			int playerclient_id;
			playerc_position2d_t *player_position;
			float player_v, player_w;
	};

	class LaserI: virtual public jderobot::Laser {
		public:
			LaserI (std::string& propertyPrefix, const jderobotice::Context& context):
			prefix(propertyPrefix),context(context),laserData(new jderobot::LaserData()) {
				Ice::PropertiesPtr prop = context.properties();

				playerserver_id=0;
				playerclient_id=0;

				// Create a client object
				cout << "Connecting to Player Server at '" << playerhost << ":" << playerport << "..." << endl;
				playerclient = playerc_client_create (NULL, playerhost.c_str(), playerport);

				// Connect to the server
				if (playerc_client_connect(playerclient) != 0)	{
					cout << "ERROR: Connecting to Player Server" << endl;
				}

				player_laser = playerc_laser_create(playerclient, 0);
				if (playerc_laser_subscribe(player_laser, PLAYER_OPEN_MODE) != 0) {
					player_laser = NULL;
					cout << "ERROR: Getting Player Laser" << endl;
				}

				/*Get laser resolution*/
				laser_num_readings = prop->getPropertyAsIntWithDefault("PlayerServer.Laser.Num_readings", NUM_LASER_DEF);
				laser_resolution = (double) prop->getPropertyAsIntWithDefault("PlayerServer.Laser.Readings_per_degree", RESOLUTION_LASER_DEF);

				laserData->numLaser = laser_num_readings;
				laserData->distanceData.resize(sizeof(int)*laserData->numLaser);
			}

			virtual ~LaserI(){};

			virtual jderobot::LaserDataPtr getLaserData(const Ice::Current&) {
				return laserData;
			};

			virtual void update() {
				int i;
				double j;
				double player_resolution;
				double jump;
				int offset;

				// Read data from the server
				if(playerc_client_read(playerclient) == NULL) {
					cout << "ERROR: Reading from Player Server" << endl;
				}	

				/*Now we must transform player resolution into user defined resolution*/
				player_resolution=(double)1.0/((double)RADTODEG*(double)player_laser->scan_res);
				jump = player_resolution / laser_resolution;

				offset=((player_laser->scan_count/jump)-laser_num_readings)/2;
				i=0;
				j=offset;

				/*Update laser values*
				while(j<player_laser->scan_count && i<laser_num_readings){
					laserData->distanceData[i]=(int)(player_laser->scan[(int)j][0]*1000);
					j=j+jump;
					i++;
				}*/
			};

		private:
			std::string prefix;
			jderobotice::Context context;
			jderobot::LaserDataPtr laserData;

			playerc_client_t *playerclient;
			int playerserver_id;
			int playerclient_id;
			playerc_laser_t *player_laser;

			int laser_num_readings;
			double laser_resolution;
	};

  class EncodersI: virtual public jderobot::Encoders {
		public:
			EncodersI (std::string& propertyPrefix, const jderobotice::Context& context):
			prefix(propertyPrefix),context(context),encodersData(new jderobot::EncodersData()) {
				Ice::PropertiesPtr prop = context.properties();

				playerserver_id=0;
				playerclient_id=0;

				// Create a client object
				cout << "Connecting to Player Server at '" << playerhost << ":" << playerport << "..." << endl;
				playerclient = playerc_client_create (NULL, playerhost.c_str(), playerport);

				// Connect to the server
				if (playerc_client_connect(playerclient) != 0)	{
					cout << "ERROR: Connecting to Player Server" << endl;
				}

				// Getting player server position
				player_position = playerc_position2d_create (playerclient, 0);  
				if (playerc_position2d_subscribe(player_position, PLAYER_OPEN_MODE) != 0)	{
					player_position = NULL;
					cout << "ERROR: Getting Player Position" << endl;
				}
				playerc_position2d_enable(player_position,1);

				correcting_x = prop->getPropertyAsIntWithDefault("PlayerServer.Initial_position.X", 0); /* mm */
				correcting_y = prop->getPropertyAsIntWithDefault("PlayerServer.Initial_position.Y", 0); /* mm */
				correcting_theta = prop->getPropertyAsIntWithDefault("PlayerServer.Initial_position.Theta", 0); /* deg */
			}

			virtual ~EncodersI(){};

			virtual jderobot::EncodersDataPtr getEncodersData(const Ice::Current&){
				return encodersData;
			};

			virtual void update() {
				float robotx, roboty, robottheta;

				// Read data from the server
				if(playerc_client_read(playerclient) == NULL) {
					cout << "ERROR: Reading from Player Server" << endl;
				}

				robotx = (player_position->px)*1000*(float)cos(DEGTORAD*correcting_theta) - (player_position->py)*1000*(float)sin(DEGTORAD*correcting_theta) + correcting_x;
				roboty = (player_position->py)*1000*(float)cos(DEGTORAD*correcting_theta) + (player_position->px)*1000*(float)sin(DEGTORAD*correcting_theta) + correcting_y;
				robottheta = (player_position->pa*RADTODEG) + correcting_theta;
				if (robottheta<=0) 
					robottheta = robottheta + 360;
				else if (robottheta > 360) 
					robottheta = robottheta - 360;

				encodersData->robotx = robotx;
				encodersData->roboty = roboty;
				encodersData->robottheta = robottheta;
				encodersData->robotcos = cos(robottheta);
				encodersData->robotsin = sin(robottheta);
			};

		private:
			std::string prefix;
			jderobotice::Context context;
			jderobot::EncodersDataPtr encodersData;
			playerc_client_t *playerclient;
			int playerserver_id;
			int playerclient_id;
			playerc_position2d_t *player_position;
			float correcting_x; /* mm */
			float correcting_y; /* mm */
			float correcting_theta; /* deg */
	};


	class PTMotorsI: virtual public jderobot::PTMotors {
		public:
			PTMotorsI(std::string& propertyPrefix, const jderobotice::Context& context):
			prefix(propertyPrefix),context(context) {
				Ice::PropertiesPtr prop = context.properties();

				playerserver_id=0;
				playerclient_id=0;

				// Create a client object
				cout << "Connecting to Player Server at '" << playerhost << ":" << playerport << "..." << endl;
				playerclient = playerc_client_create (NULL, playerhost.c_str(), playerport);

				// Connect to the server
				if (playerc_client_connect(playerclient) != 0)	{
					cout << "ERROR: Connecting to Player Server" << endl;
				}

				// Getting player server pantilt
				player_ptz = playerc_ptz_create (playerclient, 0);

				if (playerc_ptz_subscribe (player_ptz, PLAYER_OPEN_MODE) != 0) {
					player_ptz = NULL;
					cout << "ERROR: Getting Player PTZ" << endl;
				}
			}

			virtual ~PTMotorsI() {};

			virtual Ice::Int setPTMotorsData(const jderobot::PTMotorsDataPtr & data, const Ice::Current&){
				// Read data from the server
				if(playerc_client_read(playerclient) == NULL) {
					cout << "ERROR: Reading from Player Server" << endl;
				}

				//player_ptz_lock (player_ptz, 1);
				player_ptz->pan = data->longitude;
				if (data->longitude > MAX_PAN)
				player_ptz->pan = MAX_PAN;
				else if (data->longitude < -54)
				player_ptz->pan = MIN_PAN;

				player_ptz->tilt = data->latitude;
				if (data->latitude > MAX_TILT)
				player_ptz->tilt = MAX_TILT;
				else if (data->latitude < MIN_TILT)
				player_ptz->tilt = MIN_TILT;
				//player_ptz_unlock (player_ptz);

				return 0;
			};

			std::string prefix;
			jderobotice::Context context;
			jderobot::PTMotorsDataPtr ptMotorsData;

			playerc_client_t *playerclient;
			int playerserver_id;
			int playerclient_id;
			playerc_ptz_t *player_ptz;
	};

	class PTEncodersI: virtual public jderobot::PTEncoders {
		public:
			PTEncodersI(std::string& propertyPrefix, const jderobotice::Context& context):
			prefix(propertyPrefix),context(context)	{
				Ice::PropertiesPtr prop = context.properties();

				playerserver_id=0;
				playerclient_id=0;

				// Create a client object
				cout << "Connecting to Player Server at '" << playerhost << ":" << playerport << "..." << endl;
				playerclient = playerc_client_create (NULL, playerhost.c_str(), playerport);

				// Connect to the server
				if (playerc_client_connect(playerclient) != 0)	{
					cout << "ERROR: Connecting to Player Server" << endl;
				}

				// Getting player server pantilt
				player_ptz = playerc_ptz_create (playerclient, 0);

				if (playerc_ptz_subscribe (player_ptz, PLAYER_OPEN_MODE) != 0) {
					player_ptz = NULL;
					cout << "ERROR: Getting Player PTZ" << endl;
				}
			}

			virtual ~PTEncodersI(){};

			virtual jderobot::PTEncodersDataPtr getPTEncodersData(const Ice::Current&){
				// Read data from the server
				if(playerc_client_read(playerclient) == NULL) {
					cout << "ERROR: Reading from Player Server" << endl;
				}

				//player_ptz_lock (player_ptz, 1);
				ptEncodersData->panAngle = -1 * player_ptz->pan * RADTODEG;
				ptEncodersData->tiltAngle = -1 * player_ptz->tilt * RADTODEG;
				//player_ptz_unlock (player_ptz);

				return ptEncodersData; 
			};


			std::string prefix;
			jderobotice::Context context;
			jderobot::PTEncodersDataPtr ptEncodersData;

			playerc_client_t *playerclient;
			int playerserver_id;
			int playerclient_id;
			playerc_ptz_t *player_ptz;
	};

	class SonarsI: virtual public jderobot::Sonars {
		public:
			SonarsI(std::string& propertyPrefix, const jderobotice::Context& context):
			prefix(propertyPrefix),context(context),sonarsData(new jderobot::SonarsData()) {
				Ice::PropertiesPtr prop = context.properties();

				playerserver_id=0;
				playerclient_id=0;

				// Create a client object
				cout << "Connecting to Player Server at '" << playerhost << ":" << playerport << "..." << endl;
				playerclient = playerc_client_create (NULL, playerhost.c_str(), playerport);

				// Connect to the server
				if (playerc_client_connect(playerclient) != 0)	{
					cout << "ERROR: Connecting to Player Server" << endl;
				}

				player_sonar = playerc_sonar_create (playerclient, 0);
				if (playerc_sonar_subscribe (player_sonar, PLAYER_OPEN_MODE) != 0) {
					player_sonar = NULL;
					cout << "ERROR: Getting Player Sonar" << endl;
				}
				playerc_sonar_get_geom(player_sonar);

				sonarsData->numSonars = NUM_SONARS_DEF;
				sonarsData->us.resize(sizeof(int)*sonarsData->numSonars);
			}

			virtual ~SonarsI() {};

			virtual jderobot::SonarsDataPtr getSonarsData (const Ice::Current&) {
				return sonarsData; 
			};

			virtual void update() {
				int i;
				int num_sonars;

				// Read data from the server
				if(playerc_client_read(playerclient) == NULL) {
					cout << "ERROR: Reading from Player Server" << endl;
				}

				if(player_sonar->pose_count > NUM_SONARS_DEF)
					num_sonars = NUM_SONARS_DEF;
				else
					num_sonars = player_sonar->pose_count;

				/*Update sonars values**
				for (i = 0; i < num_sonars; i++) {
					sonarsData->us[i] = (int) player_sonar->scan[i]*1000;
				}*/
				sonarsData->numSonars = num_sonars;
			}

			std::string prefix;
			jderobotice::Context context;
			jderobot::SonarsDataPtr sonarsData;

			playerc_client_t *playerclient;
			int playerserver_id;
			int playerclient_id;
			playerc_sonar_t *player_sonar;
	};

	class Component: public jderobotice::Component {
		public:
			Component():jderobotice::Component("PlayerServer"),
			motors1(0), laser1(0), encoders1(0), ptmotors1(0), ptencoders1(0), sonars1(0) {}

			virtual void start() {
				int avMotors, avLaser, avEncoders;
				int avPTMotors, avPTEncoders, avSonars;

				Ice::PropertiesPtr prop = context().properties();

				playerhost = prop->getPropertyWithDefault(context().tag() + ".Hostname","localhost");
				playerport = prop->getPropertyAsIntWithDefault(context().tag() + ".Port",6665);

				//Motors
				avMotors = prop->getPropertyAsInt(context().tag() + ".Motors");
				if (avMotors == 1) {
					cout << "DEBUG: Entramos en motors" << endl;
					std::string objPrefix2(context().tag() + ".Motors");
					std::string motorsName = "motors1";
					context().tracer().info("Creating motors1 " + motorsName);
					motors1 = new MotorsI(objPrefix2,context());
					context().createInterfaceWithString(motors1,motorsName);
				}

				//Laser
				avLaser = prop->getPropertyAsInt(context().tag() + ".Laser");
				if (avLaser == 1) {
					cout << "DEBUG: Entramos en laser" << endl;
					std::string objPrefix3(context().tag() + ".Laser");
					std::string laserName = "laser1";
					context().tracer().info("Creating laser1 " + laserName);
					laser1 = new LaserI(objPrefix3,context());
					context().createInterfaceWithString(laser1,laserName);
				}

				//Encoders
				avEncoders = prop->getPropertyAsInt(context().tag() + ".Encoders");
				if (avEncoders == 1) {
					cout << "DEBUG: Entramos en encoders" << endl;
					std::string objPrefix4(context().tag() + ".Encoders");
					std::string encodersName = "encoders1";
					context().tracer().info("Creating encoders1 " + encodersName);
					encoders1 = new EncodersI(objPrefix4,context());
					context().createInterfaceWithString(encoders1,encodersName);
				}

				//PTMotors
				/*avPTMotors = prop->getPropertyAsInt(context().tag() + ".PTMotors");
				if (avPTMotors == 1) {
					cout << "DEBUG: Entramos en ptmotors" << endl;
					std::string objPrefix5(context().tag() + ".PTMotors");
					std::string ptmotorsName = "ptmotors1";
					context().tracer().info("Creating ptmotors1 " + ptmotorsName);
					ptmotors1 = new PTMotorsI(objPrefix5,context());
					context().createInterfaceWithString(ptmotors1,ptmotorsName);
				}*/

				//PTEncoders
				avPTEncoders = prop->getPropertyAsInt(context().tag() + ".PTEncoders");
				if (avPTEncoders == 1) {
					cout << "DEBUG: Entramos en ptencoders" << endl;
					std::string objPrefix6(context().tag() + ".PTEncoders");
					std::string ptencodersName = "ptencoders1";
					context().tracer().info("Creating ptencoders1 " + ptencodersName);
					ptencoders1 = new PTEncodersI(objPrefix6,context());
					context().createInterfaceWithString(ptencoders1,ptencodersName);
				}

				//Sonars
				avSonars = prop->getPropertyAsInt(context().tag() + ".Sonars");
				if (avSonars == 1) {
					cout << "DEBUG: Entramos en sonars" << endl;
					std::string objPrefix7(context().tag() + ".Sonars");
					std::string sonarsName = "sonars1";
					context().tracer().info("Creating sonars1 " + sonarsName);
					sonars1 = new SonarsI(objPrefix7,context());
					context().createInterfaceWithString(sonars1,sonarsName);
				}

				//Get subclasses
				MotorsI * motors = NULL;
				LaserI * laser = NULL;
				EncodersI * encoders = NULL;
				PTMotorsI * ptmotors = NULL;
				PTEncodersI * ptencoders = NULL;
				SonarsI * sonars = NULL;

				if(avMotors)
					motors = dynamic_cast<MotorsI*>(&(*motors1));
				if(avLaser)
					laser = dynamic_cast<LaserI*>(&(*laser1));
				if(avEncoders)
					encoders = dynamic_cast<EncodersI*>(&(*encoders1));
				//if(avPTMotors)
				//	ptmotors = dynamic_cast<PTMotorsI*>(&(*ptmotors1));
				if(avPTEncoders)
					ptencoders = dynamic_cast<PTEncodersI*>(&(*ptencoders1));			
				if(avSonars)
					sonars = dynamic_cast<SonarsI*>(&(*sonars1));		

				//Update values
				while(true) {
					if(avMotors && motors)
						  motors->update();

					if(avLaser && laser)
						  laser->update();

					if(avEncoders && encoders)
						  encoders->update();

					/*if(avPTMotors && ptmotors)
						  ptmotors->update();

					if(avPTEncoders && ptencoders)
						  ptencoders->update();*/

					if(avSonars && sonars)
						  sonars->update();

					usleep(ITERATION_TIME*1000);
				}
			}

			virtual ~Component() {}

			private:
				Ice::ObjectPtr motors1;
				Ice::ObjectPtr laser1;
				Ice::ObjectPtr encoders1;
				Ice::ObjectPtr ptmotors1;
				Ice::ObjectPtr ptencoders1;
				Ice::ObjectPtr sonars1;
	};
}

int main(int argc, char** argv) {
	playerserver::Component component;
	jderobotice::Application app(component);
	return app.jderobotMain(argc,argv);
}