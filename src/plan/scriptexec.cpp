/* 
 * Simple script executor.
 * Copyright (C) 2007,2009 Petr Kubanek <petr@kubanek.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "scriptexec.h"
#include "../../lib/rts2script/execcli.h"
#include "rts2devcliphot.h"

#include "configuration.h"

#include <iostream>
#include <fstream>
#include <vector>

using namespace rts2plan;
using namespace rts2image;

// ScriptExec class

/**
 * Print on standard output image name when image has beed created.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 */
class ClientCameraScript:public rts2script::DevClientCameraExec
{
	public:
		ClientCameraScript (rts2core::Connection *conn, rts2core::ValueString *_expandPath, std::string templateFile):rts2script::DevClientCameraExec (conn, _expandPath, templateFile) {};
		virtual imageProceRes processImage (Image * image);
};

imageProceRes ClientCameraScript::processImage (Image * image)
{
	image->saveImage ();
	std::cout << image->getFileName () << std::endl;

	return DevClientCameraExec::processImage (image);
}

int ScriptExec::findScript (std::string in_deviceName, std::string & buf)
{
	// take script from stdin
	if (deviceName)
	{
		scripts.push_back (new rts2script::ScriptForDeviceStream (std::string (deviceName), &std::cin));
		deviceName = NULL;
	}
	for (std::vector < rts2script::ScriptForDevice * >::iterator iter = scripts.begin (); iter != scripts.end (); iter++)
	{
		rts2script::ScriptForDevice *ds = *iter;
		if (ds->isDevice (in_deviceName))
		{
			if (!nextRunningQ)
			{
				time (&nextRunningQ);
				nextRunningQ += 5;
			}
			return ds->getScript (buf);
		}
	}
	return -1;
}

bool ScriptExec::isScriptRunning ()
{
	int runningScripts = 0;
	postEvent (new Rts2Event (EVENT_SCRIPT_RUNNING_QUESTION, (void *) &runningScripts));
	if (runningScripts > 0)
		return true;
	// if there are some images which need to be written
	postEvent (new Rts2Event (EVENT_NUMBER_OF_IMAGES, (void *)&runningScripts));
	if (runningScripts > 0)
		return true;
	// if we still have some commands in que, wait till they finish
	return (!commandQueEmpty ());
}

int ScriptExec::processOption (int in_opt)
{
	std::ifstream * is;
	switch (in_opt)
	{
		case OPT_CONFIG:
			configFile = optarg;
			break;
		case 'c':
		case 'd':
			deviceName = optarg;
			break;
		case 'S':
			callScriptEnd = false;
		case 's':
			if (!deviceName)
			{
				defaultScript = optarg;
			}
			else
			{
				scripts.push_back (new rts2script::ScriptForDevice (std::string (deviceName), std::string (optarg)));
				deviceName = NULL;
			}
			break;
		case 'f':
			if (!deviceName)
			{
				std::cerr << "unknow device name" << std::endl;
				return -1;
			}
			is = new std::ifstream (optarg);
			scripts.push_back (new rts2script::ScriptForDeviceStream (std::string (deviceName), is));
			deviceName = NULL;
			break;
		case 'e':
			expandPath = new rts2core::ValueString ("expand_path");
			expandPath->setValueString (optarg);
			break;
		case 't':
			templateFile = std::string (optarg);
			break;
		default:
			return rts2core::Client::processOption (in_opt);
	}
	return 0;
}

void ScriptExec::usage ()
{
	std::cout << "To get 10 2 second exposures from the camera C0, use:" << std::endl
	  << "  " << getAppName () << " -d C0 -s 'for 10 { E 2 }'" << std::endl
	  << "To change scriptexec to produce files in C0 directory, with local time, execute:" << std::endl
	  << "  " << getAppName () << " -d C0 -s 'for 10 { E 2 }' -e '%c/%L%f'" << std::endl
	  << "Coordinate with xpaset to ship new images into ds9, as soon as they are acquired -works in bash:" << std::endl
	  << "  " << "while true; do " << getAppName () << " -d C0 -S 'for 1000 { E 1 }' | while read x; do xpaset ds9 fits < $x; done ; done" << std::endl
	  << "Same as above, but delete (temporary) file after displaying it in ds9:" << std::endl
	  << "  " << "while true; do " << getAppName () << " -e /tmp/%n.fits -d C0 -S 'for 1000 { E 1 }' | while read x; do xpaset ds9 fits < $x; rm $x; done ; done" << std::endl;
}

ScriptExec::ScriptExec (int in_argc, char **in_argv):rts2core::Client (in_argc, in_argv), rts2script::ScriptInterface ()
{
	waitState = 0;
	currentTarget = NULL;
	nextRunningQ = 0;
	configFile = NULL;
	expandPath = NULL;
	templateFile = std::string ("");

	defaultScript = NULL;

	callScriptEnd = true;

	addOption (OPT_CONFIG, "config", 1, "configuration file");

	addOption ('c', NULL, 1, "name of next script camera");
	addOption ('d', NULL, 1, "name of next script device");
	addOption ('s', NULL, 1, "device script (for device specified with d)");
	addOption ('S', NULL, 1, "device script called without explicit script_ends (without device reset)");
	addOption ('f', NULL, 1, "script filename");

	addOption ('e', NULL, 1, "filename expand string, override default in configuration file");
	addOption ('t', NULL, 1, "template filename for FITS keys");

	srandom (time (NULL));
}

ScriptExec::~ScriptExec (void)
{
	for (std::vector < rts2script::ScriptForDevice * >::iterator iter = scripts.begin ();
		iter != scripts.end (); iter++)
	{
		delete *iter;
	}
	scripts.clear ();

	delete currentTarget;
	delete expandPath;
}

int ScriptExec::init ()
{
	int ret;
	ret = rts2core::Client::init ();
	if (ret)
		return ret;

	// load config..

	Configuration *config = Configuration::instance ();
	ret = config->loadFile (configFile);
	if (ret)
		return ret;

	// see if we have expand path defined in configuration file..
	if (expandPath == NULL)
	{
		std::string fp;
		config->getString ("scriptexec", "expand_path", fp, "%f");
		expandPath = new rts2core::ValueString ("expand_path");
		expandPath->setValueString (fp.c_str ());
	}

	// there is a script without device..
	if (defaultScript)
	{
		std::string devName;
		ret = Configuration::instance ()->getString ("scriptexec", "default_device", devName);
		if (ret)
		{
			std::cerr << "neither -d nor default_device configuration option was not provided" << std::endl;
			return -1;
		}
		scripts.push_back (new rts2script::ScriptForDevice (devName, std::string (defaultScript)));
	}

	// create current target
	currentTarget = new rts2script::ScriptTarget (this);

	return 0;
}

int ScriptExec::doProcessing ()
{
	return 0;
}

rts2core::DevClient *ScriptExec::createOtherType (rts2core::Connection * conn, int other_device_type)
{
	rts2core::DevClient *cli;
	switch (other_device_type)
	{
		case DEVICE_TYPE_MOUNT:
			cli = new rts2script::DevClientTelescopeExec (conn);
			break;
		case DEVICE_TYPE_CCD:
			cli = new ClientCameraScript (conn, expandPath, templateFile);
			break;
		case DEVICE_TYPE_FOCUS:
			cli = new rts2image::DevClientFocusImage (conn);
			break;
			/*    case DEVICE_TYPE_PHOT:
				  cli = new Rts2DevClientPhotExec (conn);
				  break; */
		case DEVICE_TYPE_DOME:
		case DEVICE_TYPE_SENSOR:
			cli = new rts2image::DevClientWriteImage (conn);
			break;
		default:
			cli = rts2core::Client::createOtherType (conn, other_device_type);
	}
	return cli;
}

void ScriptExec::postEvent (Rts2Event * event)
{
	switch (event->getType ())
	{
		case EVENT_SCRIPT_ENDED:
			break;
		case EVENT_MOVE_OK:
			if (waitState)
			{
				postEvent (new Rts2Event (EVENT_CLEAR_WAIT));
				break;
			}
			//      postEvent (new Rts2Event (EVENT_OBSERVE));
			break;
		case EVENT_MOVE_FAILED:
			//endRunLoop ();
			break;
		case EVENT_ENTER_WAIT:
			waitState = 1;
			break;
		case EVENT_CLEAR_WAIT:
			waitState = 0;
			break;
		case EVENT_GET_ACQUIRE_STATE:
			*((int *) event->getArg ()) = 1;
			//      (currentTarget) ? currentTarget->getAcquired () : -2;
			break;
	}
	rts2core::Client::postEvent (event);
}

void ScriptExec::deviceReady (rts2core::Connection * conn)
{
	conn->postEvent (new Rts2Event (callScriptEnd ? EVENT_SET_TARGET : EVENT_SET_TARGET_NOT_CLEAR, (void *) currentTarget));
	conn->postEvent (new Rts2Event (EVENT_OBSERVE));
}

int ScriptExec::idle ()
{
	if (nextRunningQ != 0)
	{
		time_t now;
		time (&now);
		if (nextRunningQ < now)
		{
			if (!isScriptRunning ())
				endRunLoop ();
			nextRunningQ = now + 5;
		}
	}
	return rts2core::Client::idle ();
}

void ScriptExec::deviceIdle (rts2core::Connection * conn)
{
	if (!isScriptRunning ())
		endRunLoop ();
}

void ScriptExec::getPosition (struct ln_equ_posn *pos, double JD)
{
	pos->ra = 20;
	pos->dec = 20;
}

int main (int argc, char **argv)
{
	ScriptExec app = ScriptExec (argc, argv);
	return app.run ();
}
