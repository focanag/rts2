#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <mcheck.h>
#include <math.h>
#include <stdarg.h>

#include <argz.h>

#include "sbig.h"
#include "../utils/hms.h"
#include "../utils/devdem.h"
#include "../status.h"

#define PORT    5556

int port;

struct sbig_init info;
struct sbig_readout readout[2];

float readout_comp[2];

pthread_t thread[2];

int
complete (int ccd, float percent_complete)
{
  readout_comp[ccd] = percent_complete;
  return 1;
}

// wrapper to call sbig expose in thread, test for results
void *
start_expose (void *arg)
{
  int ret;
  if ((ret = sbig_expose (arg)) < 0)
    {
      syslog (LOG_ERR, "error during chip %i exposure: %s",
	      ((struct sbig_expose *) arg)->ccd, sbig_show_error (ret));
      return NULL;
    }
  syslog (LOG_INFO, "exposure chip %i finished.",
	  ((struct sbig_expose *) arg)->ccd);
  devdem_status_mask (((struct sbig_readout *) arg)->ccd,
		      CAM_MASK_EXPOSE,
		      CAM_NOTREADING & CAM_DATA, "exposure chip finished");
  return NULL;
}

// wrapper to call sbig readout in thread 
void *
start_readout (void *arg)
{
  int ret;
  if ((ret = sbig_readout (arg)) < 0)
    {
      syslog (LOG_ERR, "error during chip %i readout: %s",
	      ((struct sbig_readout *) arg)->ccd, sbig_show_error (ret));
      return NULL;
    }
  syslog (LOG_INFO, "reading chip %i finished.",
	  ((struct sbig_readout *) arg)->ccd);
  devdem_status_mask (((struct sbig_readout *) arg)->ccd,
		      CAM_MASK_READING | CAM_MASK_DATA,
		      CAM_NOTREADING & CAM_DATA, "reading chip finished");
  return NULL;
}

// macro for length test
#define test_length(npars) if (argz_count (argv, argc) != npars + 1) { \
        devdem_write_command_end (DEVDEM_E_PARAMSNUM, \
		"bad nmbr of params: expected %i,got %i",\
		npars, argz_count (argv, argc) - 1 ); \
	return -1; \
}


// macro for chip test
#define get_chip  param = argz_next (argv, argc, argv); \
      chip = strtol(param, &endptr, 10); \
      if ((*endptr) || (chip < 0) || (chip >= info.nmbr_chips)) \
        {      \
	  devdem_write_command_end (DEVDEM_E_PARAMSVAL, "invalid chip: %f", chip);\
	  return -1;\
	}

// Macro for camera call
# define cam_call(call) if ((ret = call) < 0)\
{\
	devdem_write_command_end (DEVDEM_E_HW, "camera error: %s", sbig_show_error (ret));\
        return -1; \
}


/*! Handle camd command.
 *
 * @param buffer buffer, containing unparsed command
 * @param fd descriptor of input/output socket
 * @return -2 on exit, -1 and set errno on HW failure, 0 otherwise
 */
int
camd_handle_command (char *argv, size_t argc)
{
  int ret;
  char *param;
  char *endptr;
  int chip;

  if (strcmp (argv, "ready") == 0)
    {
      cam_call (sbig_init (port, 5, &info));
    }
  else if (strcmp (argv, "info") == 0)
    {
      cam_call (sbig_init (port, 5, &info));
      devdem_dprintf ("name %s", info.camera_name);
      devdem_dprintf ("type %i", info.camera_type);
      devdem_dprintf ("serial %10s", info.serial_number);
      devdem_dprintf ("abg %i", info.imaging_abg_type);
      devdem_dprintf ("chips %i", info.nmbr_chips);
    }
  else if (strcmp (argv, "chipinfo") == 0)
    {
      int i;
      readout_mode_t *mode;
      test_length (1);
      get_chip;
      mode = (readout_mode_t *) (&info.camera_info[chip].readout_mode[0]);
      devdem_dprintf ("binning %i", readout[chip].binning);
      devdem_dprintf ("readout_modes %i",
		      info.camera_info[chip].nmbr_readout_modes);
      for (i = 0; i < info.camera_info[chip].nmbr_readout_modes; i++)
	{
	  devdem_dprintf ("mode %i %i %i %i %i %i", mode->mode, mode->width,
			  mode->height, mode->gain, mode->pixel_width,
			  mode->pixel_height);
	  mode++;
	}
      ret = 0;
    }
  else if (strcmp (argv, "expose") == 0)
    {
      float exptime;
      test_length (2);
      get_chip;
      param = argz_next (argv, argc, param);
      exptime = strtof (param, NULL);
      if ((exptime <= 0) || (exptime > 330000))
	{
	  devdem_write_command_end (DEVDEM_E_PARAMSVAL,
				    "invalid exposure time: %f", exptime);
	}
      else
	{
	  struct sbig_expose expose;
	  expose.ccd = chip;
	  expose.exposure_time = exptime;
	  expose.abg_state = 0;
	  expose.shutter = 0;
	  if ((ret =
	       pthread_create (&thread[chip], NULL, start_expose,
			       (void *) &expose)))
	    {
	      devdem_write_command_end (DEVDEM_E_SYSTEM,
					"while creating thread for execution: %s",
					strerror (errno));
	      return -1;
	    }
	  devdem_status_mask (chip, CAM_MASK_EXPOSE, CAM_EXPOSING,
			      "starting exposure on chip");
	}
    }
  else if (strcmp (argv, "stopexpo") == 0)
    {
      test_length (1);
      get_chip;
      cam_call (sbig_end_expose (chip));
      devdem_status_mask (chip, CAM_MASK_EXPOSE, CAM_NOEXPOSURE,
			  "exposure was stopped");
    }
  else if (strcmp (argv, "progexpo") == 0)
    {
      test_length (1);
      get_chip;
      errno = ENOTSUP;
      ret = -1;
    }
  else if (strcmp (argv, "readout") == 0)
    {
      int mode;
      test_length (1);
      get_chip;
      readout[chip].ccd = chip;
      readout[chip].x = readout[chip].y = 0;
      mode = readout[chip].binning;
      readout[chip].width = info.camera_info[chip].readout_mode[mode].width;
      readout[chip].height = info.camera_info[chip].readout_mode[mode].height;
      readout[chip].callback = complete;

      if ((ret =
	   pthread_create (&thread[chip], NULL, start_readout,
			   (void *) &readout[chip])))
	{
	  devdem_write_command_end (DEVDEM_E_SYSTEM,
				    "while creating thread for execution: %s",
				    strerror (errno));
	  return -1;
	}
      devdem_status_mask (chip, CAM_MASK_READING, CAM_READING,
			  "reading started");
    }
  else if (strcmp (argv, "binning") == 0)
    {
      int new_bin;
      test_length (2);
      get_chip;
      param = argz_next (argv, argc, param);
      new_bin = strtol (param, &param, 10);
      if (*param || new_bin < 0
	  || new_bin >= info.camera_info[chip].nmbr_readout_modes)
	{
	  devdem_write_command_end (DEVDEM_E_PARAMSVAL, "invalid binning: %i",
				    new_bin);
	  return -1;
	}
      readout[chip].binning = (unsigned int) new_bin;
      ret = 0;
    }
  else if (strcmp (argv, "stopread") == 0)
    {
      test_length (1);
      get_chip;
      if ((ret = pthread_cancel (thread[chip])))
	{
	  devdem_write_command_end (DEVDEM_E_SYSTEM,
				    "while canceling thread: %s",
				    strerror (errno));
	  return -1;
	}
      readout_comp[chip] = -1;
    }
  else if (strcmp (argv, "status") == 0)
    {
      int i;
      test_length (1);
      get_chip;
      for (i = 0; i < 3; i++)
	{
	  devdem_status_message (i, "status request");
	}
      devdem_dprintf ("readout %f", readout_comp[chip]);
      ret = 0;
    }
  else if (strcmp (argv, "data") == 0)
    {
      test_length (1);
      get_chip;
      if (readout[chip].data_size_in_bytes == 0)
	{
	  devdem_write_command_end (DEVDEM_E_SYSTEM, "data not available");
	  return -1;
	}
      ret =
	devdem_send_data (NULL, readout[chip].data,
			  readout[chip].data_size_in_bytes);
    }
  else if (strcmp (argv, "cool_temp") == 0)
    {
      char *endpar;
      struct sbig_cool cool;
      test_length (1);
      param = argz_next (argv, argc, argv);
      cool.temperature = round (strtod (param, &endpar) * 10);
      if (endpar && !*endpar)
	{
	  devdem_write_command_end (DEVDEM_E_PARAMSVAL,
				    "invalid temperature: %s", param);
	  return -1;
	}
      cool.regulation = 1;
      cam_call (sbig_set_cooling (&cool));
    }
  else if (strcmp (argv, "exit") == 0)
    {
      return -2;
    }
  else if (strcmp (argv, "help") == 0)
    {
      devdem_dprintf ("ready - is camera ready?");
      devdem_dprintf ("info - information about camera");
      devdem_dprintf ("chipinfo <chip> - information about chip");
      devdem_dprintf
	("expose <chip> <exposure> - start exposition on given chip");
      devdem_dprintf ("stopexpo <chip> - stop exposition on given chip");
      devdem_dprintf ("progexpo <chip> - query exposition progress");
      devdem_dprintf ("readout <chip> - start reading given chip");
      devdem_dprintf
	("binning <chip> <binning_id> - set new binning; actual from next readout on");
      devdem_dprintf ("stopread <chip> - stop reading given chip");
      devdem_dprintf
	("data <chip> - establish data connection on given port and address");
      devdem_dprintf ("cool_temp <temp> - cooling temperature");
      devdem_dprintf ("exit - exit from connection");
      devdem_dprintf ("help - print, what you are reading just now");
      ret = errno = 0;
      goto end;
    }
  else
    {
      devdem_write_command_end (DEVDEM_E_COMMAND, "unknow command: %s", argv);
      return -1;
    }

end:
  return ret;
}

int
main (void)
{
  int i;
  char *stats[] = { "img_chip", "trc_chip" };
  port = 2;
#ifdef DEBUG
  mtrace ();
#endif
  for (i = 0; i < 2; i++)
    readout[i].data = NULL;

  // open syslog
  openlog (NULL, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

  info.nmbr_chips = -1;
  return devdem_run (PORT, camd_handle_command, stats, 2);
}
