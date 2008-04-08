/* 
 * User management.
 * Copyright (C) 2008 Petr Kubanek <petr@kubanek.net>
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

#ifndef __RTS2_USER__
#define __RTS2_USER__

#include <string>
#include <ostream>
#include <list>

/**
 * Represents type which user have subscribed for receiving events.
 */
class Rts2TypeUser
{
	private:
		char type;
		int eventMask;
	public:
		Rts2TypeUser (char type, int eventMask);
		~Rts2TypeUser (void);

		friend std::ostream & operator << (std::ostream & _os, Rts2TypeUser & usr);
};

std::ostream & operator << (std::ostream & _os, Rts2TypeUser & usr);

class Rts2TypeUserSet: public std::list <Rts2TypeUser>
{
	private:
		int load (int usr_id);
	public:
		/**
		 * Load type-user map for given user ID.
		 */
		Rts2TypeUserSet (int usr_id);
		~Rts2TypeUserSet (void);

		/**
		 * Add to user new entry about which events he/she
		 * should receive from given observation.
		 *
		 * @param id    User id.
		 * @param type  Target type.
		 * @param flags Event flags (when to trigger the event).
		 * @return -1 on error, 0 on success.
		 */
		int addNewTypeFlags (int id, char type, int flags);
};

std::ostream & operator << (std::ostream & _os, Rts2TypeUserSet & usr);

/**
 * Represents user from database.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 */
class Rts2User
{
	private:
		int id;
		std::string login;
		std::string email;

		Rts2TypeUserSet *types;

	public:
		/**
		 * Construct empty user object.
		 * You should call some load method to load data from database.
		 */
		Rts2User ();

		/**
		 * Construct user from database entry.
		 * This constructor is passed all user entries. It does not extract any
		 * information from the database.
		 *
		 * @param in_id     User ID.
		 * @param in_login  User login.
		 * @param in_email  User email.
		 */
		Rts2User (int in_id, std::string in_login, std::string in_email);
		~Rts2User (void);

		/**
		 * Load user entry from the database.
		 *
		 * @param in_login User login.
		 * @return -1 on error, 0 on succes.
		 */
		int load (const char * in_login);

		/**
		 * Load types which belongs to given user id.
		 *
		 * @return -1 on error, 0 on sucess.
		 */
		int loadTypes ();

		/**
		 * Sets user password.
		 *
		 * @param newPass  New user password.
		 * @return -1 on error, 0 on success.
		 */
		int setPassword (std::string newPass);


		/**
		 * Sets user email.
		 *
		 * @param newEmail New user email.
		 * @return -1 on error, 0 on succes.
		 */
		int setEmail (std::string newEmail);

		/**
		 * Add to user new entry about which events he/she
		 * should receive from given observation.
		 *
		 * @param type  Target type.
		 * @param flags Event flags (when to trigger the event).
		 * @return -1 on error, 0 on success.
		 */
		int addNewTypeFlags (char type, int flags)
		{
			return types->addNewTypeFlags (id, type, flags);
		}

		/**
		 * Prints Rts2User object to a stream.
		 *
		 * @param _os  Stream to which object will be printed.
		 * @param user User which will be printed.
		 * @return Stream with printed user.
		 */
		friend std::ostream & operator << (std::ostream & _os, Rts2User & user);
};

std::ostream & operator << (std::ostream & _os, Rts2User & user);

/**
 * Verify username and password combination.
 *
 * @param username   User login name.
 * @param pass       User password.
 *
 * @return True if login and password is correct, false otherwise.
 */
bool verifyUser (std::string username, std::string pass);
#endif							 /* !__RTS2_USER__ */
