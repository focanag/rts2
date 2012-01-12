#include "../../lib/rts2fits/rts2appdbimage.h"
#include "../../lib/rts2fits/imagedb.h"
#include "configuration.h"

#include <iostream>

#include <list>

class Rts2MoveArchive:public Rts2AppDbImage
{
	public:
		Rts2MoveArchive (int in_argc, char **in_argv):Rts2AppDbImage (in_argc, in_argv, true)
		{
			rts2core::Configuration *config;
			config = rts2core::Configuration::instance ();
		}

	protected:
		virtual int processImage (rts2image::ImageDb * image)
		{
			double val;
			int ret;
			std::cout << "Processing " << image->getFileName () << "..";
			try
			{
				image->getValue ("CRVAL1", val);
				ret = image->toArchive ();
				std::cout << (ret ? "failed (cannot move?)" : "archive") << std::endl;
			}
			catch (rts2core::Error &er)
			{
				ret = image->toTrash ();
				std::cout << (ret ? "failed (cannot move?)" : "trash") << std::endl;
			}
			return 0;
		}
};

int main (int argc, char **argv)
{
	Rts2MoveArchive app = Rts2MoveArchive (argc, argv);
	return app.run ();
}
