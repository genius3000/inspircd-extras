/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2019 Peter Powell <petpow@saberuk.com>
 *
 * This file is part of InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// $ModAuthor: Peter "SaberUK" Powell
/// $ModAuthorMail: petpow@saberuk.com
/// $ModDepends: core 3
/// $ModDesc: Allows services to add custom tags to messages sent by clients.


#include "inspircd.h"
#include "modules/cap.h"
#include "modules/who.h"

typedef insp::flat_map<std::string, std::string, irc::insensitive_swo> CustomTagMap;
typedef insp::flat_map<std::string, size_t, irc::insensitive_swo> SpecialMessageMap;

class CustomTagsExtItem : public SimpleExtItem<CustomTagMap>
{
 public:
	CustomTagsExtItem(Module* Creator)
		: SimpleExtItem<CustomTagMap>("custom-tags", ExtensionItem::EXT_USER, Creator)
	{
	}

	void FromNetwork(Extensible* container, const std::string& value) CXX11_OVERRIDE
	{
		User* user = static_cast<User*>(container);
		if (!user)
			return;

		CustomTagMap* list = new CustomTagMap();
		irc::spacesepstream ts(value);
		while (!ts.StreamEnd())
		{
			std::string tagname;
			std::string tagvalue;
			if (!ts.GetToken(tagname) || !ts.GetToken(tagvalue))
			{
				ServerInstance->Logs->Log(MODNAME, LOG_DEBUG, "Malformed tag list received for %s: %s",
					user->uuid.c_str(), value.c_str());
				delete list;
				return;
			}

			list->insert(std::make_pair(tagname, tagvalue));
		}

		if (!list->empty())
		{
			set(user, list);
		}
		else
		{
			unset(container);
			delete list;
		}
	}

	std::string ToNetwork(const Extensible* container, void* item) const CXX11_OVERRIDE
	{
		CustomTagMap* list = static_cast<CustomTagMap*>(item);
		std::string buf;
		for (CustomTagMap::const_iterator iter = list->begin(); iter != list->end(); ++iter)
		{
			if (iter != list->begin())
				buf.push_back(' ');

			buf.append(iter->first);
			buf.push_back(' ');
			buf.append(iter->second);
		}
		return buf;
	}
};

class CustomTags : public ClientProtocol::MessageTagProvider
{
 private:
	Cap::Reference ctctagcap;
	CustomTagsExtItem ext;

	User* UserFromMsg(ClientProtocol::Message& msg)
	{
		SpecialMessageMap::const_iterator iter = specialmsgs.find(msg.GetCommand());
		if (iter == specialmsgs.end())
			return NULL; // Not a special message.

		size_t nick_index = iter->second;
		if (irc::equals(msg.GetCommand(), "354"))
		{
			// WHOX gets special treatment as the nick field isn't in a static position.
			if (whox_index == -1)
				return NULL; // No nick field.

			nick_index = whox_index + 1;
		}

		if (msg.GetParams().size() <= nick_index)
			return NULL; // Not enough params.

		return ServerInstance->FindNickOnly(msg.GetParams()[nick_index]);
	}

 public:
	SpecialMessageMap specialmsgs;
	std::string vendor;
	int whox_index;

	CustomTags(Module* mod)
		: ClientProtocol::MessageTagProvider(mod)
		, ctctagcap(mod, "message-tags")
		, ext(mod)
		, whox_index(-1)
	{
	}

	void OnPopulateTags(ClientProtocol::Message& msg) CXX11_OVERRIDE
	{
		User* user = msg.GetSourceUser();
		if (!user || IS_SERVER(user))
		{
			user = UserFromMsg(msg);
			if (!user)
				return; // No such user.
		}

		CustomTagMap* tags = ext.get(user);
		if (!tags)
			return;

		for (CustomTagMap::const_iterator iter = tags->begin(); iter != tags->end(); ++iter)
			msg.AddTag(vendor + "/" + iter->first, this, iter->second);
	}

	bool ShouldSendTag(LocalUser* user, const ClientProtocol::MessageTagData& tagdata) CXX11_OVERRIDE
	{
		return ctctagcap.get(user);
	}
};

class ModuleCustomTags
	: public Module
	, public Who::EventListener
{
 private:
	CustomTags ctags;

 public:
	ModuleCustomTags()
		: Who::EventListener(this)
		, ctags(this)
	{
	}

	ModResult OnWhoLine(const Who::Request& request, LocalUser* source, User* user, Membership* memb, Numeric::Numeric& numeric) CXX11_OVERRIDE
	{
		size_t nick_index;
		ctags.whox_index = request.GetFieldIndex('n', nick_index) ? nick_index : -1;
		return MOD_RES_PASSTHRU;
	}

	void ReadConfig(ConfigStatus& status) CXX11_OVERRIDE
	{
		SpecialMessageMap specialmsgs;
		ConfigTagList tags = ServerInstance->Config->ConfTags("specialmsg");
		for (ConfigIter i = tags.first; i != tags.second; ++i)
		{
			ConfigTag* tag = i->second;

			const std::string command = tag->getString("command");
			if (command.empty())
				throw ModuleException("<specialmsg:command> must be a S2C command name!");

			specialmsgs[command] = tag->getUInt("index", 0, 0, 20);
		}
		std::swap(specialmsgs, ctags.specialmsgs);

		ConfigTag* tag = ServerInstance->Config->ConfValue("customtags");
		ctags.vendor = tag->getString("vendor", ServerInstance->Config->ServerName, 1);
	}

	Version GetVersion() CXX11_OVERRIDE
	{
		return Version("Allows services to add custom tags to messages sent by clients");
	}
};

MODULE_INIT(ModuleCustomTags)
