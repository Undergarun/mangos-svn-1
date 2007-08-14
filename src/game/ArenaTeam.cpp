/* 
* Copyright (C) 2005,2006,2007 MaNGOS <http://www.mangosproject.org/>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "WorldPacket.h"
#include "ObjectMgr.h"
#include "ArenaTeam.h"

ArenaTeam::ArenaTeam()
{
    Id              = 0;
    Type            = 0;
    Name            = "";
    CaptainGuid     = 0;
    EmblemStyle     = 0;        // icon
    EmblemColor     = 0;        // icon color
    BorderStyle     = 0;        // border
    BorderColor     = 0;        // border color
    BackgroundColor = 0;        // background
    stats.games     = 0;
    stats.played    = 0;
    stats.rank      = 0;
    stats.rating    = 0;
    stats.wins      = 0;
    stats.wins2     = 0;
}

ArenaTeam::~ArenaTeam()
{

}

bool ArenaTeam::create(uint64 captainGuid, uint32 type, std::string ArenaTeamName)
{
    if(!objmgr.GetPlayer(captainGuid))              // player not exist
        return false;
    if(objmgr.GetArenaTeamByName(ArenaTeamName))    // arena team with this name already exist
        return false;

    sLog.outDebug("GUILD: creating arena team %s to leader: %u", ArenaTeamName.c_str(), GUID_LOPART(CaptainGuid));

    CaptainGuid = captainGuid;
    Name = ArenaTeamName;
    Type = type;

    QueryResult *result = sDatabase.Query("SELECT MAX(`arenateamid`) FROM `arena_team`");
    if( result )
    {
        Id = (*result)[0].GetUInt32()+1;
        delete result;
    }
    else Id = 1;

    // ArenaTeamName already assigned to ArenaTeam::name, use it to encode string for DB
    sDatabase.escape_string(ArenaTeamName);

    sDatabase.BeginTransaction();
    // sDatabase.PExecute("DELETE FROM `arena_team` WHERE `arenateamid`='%u'", Id); - MAX(`arenateam`)+1 not exist
    sDatabase.PExecute("DELETE FROM `arena_team_member` WHERE `arenateamid`='%u'", Id);
    sDatabase.PExecute("INSERT INTO `arena_team` (`arenateamid`,`name`,`captainguid`,`type`,`EmblemStyle`,`EmblemColor`,`BorderStyle`,`BorderColor`,`BackgroundColor`) "
        "VALUES('%u','%s','%u','%u','%u','%u','%u','%u','%u')",
        Id, ArenaTeamName.c_str(), GUID_LOPART(CaptainGuid), Type, EmblemStyle, EmblemColor, BorderStyle, BorderColor, BackgroundColor);
    sDatabase.PExecute("INSERT INTO `arena_team_stats` (`arenateamid`, `rating`, `games`, `wins`, `played`, `wins2`, `rank`) VALUES ('%u', '0', '0', '0', '0', '0', '0')", Id);
    sDatabase.CommitTransaction();

    AddMember(CaptainGuid);
    return true;
}

bool ArenaTeam::AddMember(uint64 PlayerGuid)
{
    std::string plName;
    uint8 plClass;

    if(!objmgr.GetPlayerNameByGUID(PlayerGuid, plName))             // player doesnt exist
        return false;
    if(Player::GetArenaTeamIdFromDB(PlayerGuid, GetSlot()) != 0)    // player already in arenateam of that size
        return false;

    // remove all player signs from another petitions
    // this will be prevent attempt joining player to many arenateams and corrupt arena team data integrity
    Player::RemovePetitionsAndSigns(PlayerGuid, GetType());

    Player *pl = objmgr.GetPlayer(PlayerGuid);
    if(pl)
    {
        plClass = (uint8)pl->getClass();
    }
    else
    {
        QueryResult *result = sDatabase.PQuery("SELECT `class` FROM `character` WHERE `guid`='%u'", GUID_LOPART(PlayerGuid));
        if(!result)
            return false;
        plClass = (*result)[0].GetUInt8();
        delete result;
    }

    ArenaTeamMember newmember;
    newmember.name          = plName;
    newmember.guid          = PlayerGuid;
    newmember.Class         = plClass;
    newmember.played_season = 0;
    newmember.played_week   = 0;
    newmember.wons_season   = 0;
    newmember.wons_week     = 0;
    members.push_back(newmember);

    sDatabase.PExecute("INSERT INTO `arena_team_member` (`arenateamid`,`guid`) VALUES ('%u', '%u')", Id, GUID_LOPART(newmember.guid));

    if(pl)
    {
        pl->SetInArenaTeam(Id, GetSlot());
        pl->SetArenaTeamIdInvited(0);
    }
    else
    {
        Player::SetUInt32ValueInDB(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + (GetSlot() * 5), Id, PlayerGuid);
    }

    // hide promote/remove buttons
    if(CaptainGuid != PlayerGuid)
    {
        if(pl)
            pl->SetUInt32Value(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + 1 + (GetSlot() * 5), 1);
        else
            Player::SetUInt32ValueInDB(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + 1 + (GetSlot() * 5), 1, PlayerGuid);
    }
    return true;
}

bool ArenaTeam::LoadArenaTeamFromDB(uint32 ArenaTeamId)
{
    LoadStatsFromDB(ArenaTeamId);
    LoadMembersFromDB(ArenaTeamId);

    //                                              0               1   2               3       4           5               6           7               8
    QueryResult *result = sDatabase.PQuery("SELECT `arenateamid`,`name`,`captainguid`,`type`,`EmblemStyle`,`EmblemColor`,`BorderStyle`,`BorderColor`,`BackgroundColor` FROM `arena_team` WHERE `arenateamid` = '%u'", ArenaTeamId);

    if(!result)
        return false;

    Field *fields = result->Fetch();

    Id = fields[0].GetUInt32();
    Name = fields[1].GetCppString();
    CaptainGuid  = MAKE_GUID(fields[2].GetUInt32(), HIGHGUID_PLAYER);
    Type = fields[3].GetUInt32();
    EmblemStyle = fields[4].GetUInt32();
    EmblemColor = fields[5].GetUInt32();
    BorderStyle = fields[6].GetUInt32();
    BorderColor = fields[7].GetUInt32();
    BackgroundColor = fields[8].GetUInt32();

    delete result;

    return true;
}

void ArenaTeam::LoadStatsFromDB(uint32 ArenaTeamId)
{
    //                                              0       1       2       3       4       5
    QueryResult *result = sDatabase.PQuery("SELECT `rating`,`games`,`wins`,`played`,`wins2`,`rank` FROM `arena_team_stats` WHERE `arenateamid` = '%u'", ArenaTeamId);

    if(!result)
        return;

    Field *fields = result->Fetch();

    stats.rating    = fields[0].GetUInt32();
    stats.games     = fields[1].GetUInt32();
    stats.wins      = fields[2].GetUInt32();
    stats.played    = fields[3].GetUInt32();
    stats.wins2     = fields[4].GetUInt32();
    stats.rank      = fields[5].GetUInt32();

    delete result;
}

void ArenaTeam::LoadMembersFromDB(uint32 ArenaTeamId)
{
    Field *fields;

    QueryResult *result = sDatabase.PQuery("SELECT `guid`,`played_week`,`wons_week`,`played_season`,`wons_season` FROM `arena_team_member` WHERE `arenateamid` = '%u'", ArenaTeamId);
    if(!result)
        return;

    do
    {
        fields = result->Fetch();
        ArenaTeamMember newmember;
        newmember.guid          = MAKE_GUID(fields[0].GetUInt32(), HIGHGUID_PLAYER);
        LoadPlayerStats(&newmember);
        newmember.played_week   = fields[1].GetUInt32();
        newmember.wons_week     = fields[2].GetUInt32();
        newmember.played_season = fields[3].GetUInt32();
        newmember.wons_season   = fields[4].GetUInt32();
        members.push_back(newmember);
    }while( result->NextRow() );
    delete result;
}

void ArenaTeam::LoadPlayerStats(ArenaTeamMember *member)
{
    Field *fields;

    QueryResult *result = sDatabase.PQuery("SELECT `name`,`class` FROM `character` WHERE `guid` = '%u'", GUID_LOPART(member->guid));
    if(!result) return;
    fields = result->Fetch();
    member->name  = fields[0].GetCppString();
    member->Class = fields[1].GetUInt8();

    delete result;
}

void ArenaTeam::SetCaptain(uint64 guid)
{
    // disable remove/promote buttons
    Player *oldcaptain = objmgr.GetPlayer(GetCaptain());
    if(oldcaptain)
        oldcaptain->SetUInt32Value(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + 1 + (GetSlot() * 5), 1);
    else
        Player::SetUInt32ValueInDB(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + 1 + (GetSlot() * 5), 1, GetCaptain());

    // set new captain
    CaptainGuid = guid;

    // update database
    sDatabase.PExecute("UPDATE `arena_team` SET `captainguid` = '%u' WHERE `arenateamid` = '%u'", GUID_LOPART(guid), Id);

    // enable remove/promote buttons
    Player *newcaptain = objmgr.GetPlayer(guid);
    if(newcaptain)
        newcaptain->SetUInt32Value(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + 1 + (GetSlot() * 5), 0);
    else
        Player::SetUInt32ValueInDB(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + 1 + (GetSlot() * 5), 0, guid);
}

void ArenaTeam::DelMember(uint64 guid)
{
    MemberList::iterator itr;
    for (itr = members.begin(); itr != members.end(); itr++)
    {
        if (itr->guid == guid)
        {
            members.erase(itr);
            break;
        }
    }

    Player *player = objmgr.GetPlayer(guid);
    if(player)
    {
        player->SetInArenaTeam(0, GetSlot());
        player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, GetName(), "", 0);
    }
    else
    {
        Player::SetUInt32ValueInDB(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + (GetSlot() * 5), 0, guid);
    }

    sDatabase.PExecute("DELETE FROM `arena_team_member` WHERE `guid` = '%u'", GUID_LOPART(guid));
}

void ArenaTeam::Disband(WorldSession *session)
{
    // event
    WorldPacket data;
    session->BuildArenaTeamEventPacket(&data, ERR_ARENA_TEAM_DISBANDED_S, 2, session->GetPlayerName(), GetName(), "");
    BroadcastPacket(&data);

    uint32 count = members.size();
    uint64 *memberGuids = new uint64[count];

    MemberList::iterator itr;
    uint32 i=0;
    for(itr = members.begin(); itr != members.end(); itr++)
    {
        memberGuids[i] = itr->guid;
        i++;
    }

    for(uint32 j = 0; j < count; j++)
        DelMember(memberGuids[j]);
    delete[] memberGuids;

    sDatabase.BeginTransaction();
    sDatabase.PExecute("DELETE FROM `arena_team` WHERE `arenateamid` = '%u'", Id);
    sDatabase.PExecute("DELETE FROM `arena_team_stats` WHERE `arenateamid` = '%u'", Id);
    sDatabase.CommitTransaction();
    objmgr.RemoveArenaTeam(this);
}

void ArenaTeam::Roster(WorldSession *session)
{
    Player *pl = NULL;

    WorldPacket data(SMSG_ARENA_TEAM_ROSTER, 100);
    data << GetSlot();                          // slot
    data << GetMembersSize();                   // members count
    data << uint32(0);                          // unknown (may be arena team id?)

    for (MemberList::iterator itr = members.begin(); itr != members.end(); ++itr)
    {
        pl = objmgr.GetPlayer(itr->guid);
        if(pl)
        {
            data << pl->GetGUID();              // guid
            data << uint8(1);                   // online flag
            data << pl->GetName();              // member name
            data << pl->GetZoneId();            // unknown, probably rank or zone (0 and 1 ?)
            data << uint8(pl->getLevel());      // unknown, probably level
            data << pl->getClass();             // class
            data << itr->played_week;           // played this week
            data << itr->wons_week;             // wins this week
            data << itr->played_season;         // played this season
            data << itr->wons_season;           // wins this season
        }
        else
        {
            data << itr->guid;                  // guid
            data << uint8(0);                   // online flag
            data << itr->name;                  // member name
            data << uint32(0);                  // unknown, zone id?
            data << uint8(0);                   // unknown, level?
            data << itr->Class;                 // class
            data << itr->played_week;           // played this week
            data << itr->wons_week;             // wins this week
            data << itr->played_season;         // played this season
            data << itr->wons_season;           // wins this season
        }
    }
    session->SendPacket(&data);
    sLog.outDebug("WORLD: Sent SMSG_ARENA_TEAM_ROSTER");
}

void ArenaTeam::Query(WorldSession *session)
{
    WorldPacket data(SMSG_ARENA_TEAM_QUERY_RESPONSE, 4*7+GetName().size()+1);
    data << GetId();                            // team id
    data << GetName();                          // team name
    data << GetType();                          // arena team type (2=2x2, 3=3x3 or 5=5x5)
    data << EmblemStyle;                        // emblem style?
    data << EmblemColor;                        // emblem color?
    data << BorderStyle;                        // border style?
    data << BorderColor;                        // border color?
    data << BackgroundColor;                    // background color?
    session->SendPacket(&data);
    sLog.outDebug("WORLD: Sent SMSG_ARENA_TEAM_QUERY_RESPONSE");
}

void ArenaTeam::Stats(WorldSession *session)
{
    WorldPacket data(SMSG_ARENA_TEAM_STATS, 4*7);
    data << GetId();                            // arena team id
    data << stats.rating;                       // rating
    data << stats.games;                        // games
    data << stats.wins;                         // wins
    data << stats.played;                       // played
    data << stats.wins2;                        // wins(again o_O)
    data << stats.rank;                         // rank
    session->SendPacket(&data);
}

void ArenaTeam::InspectStats(WorldSession *session)
{
    WorldPacket data(MSG_INSPECT_ARENA_STATS, 8+1+4*5);
    data << session->GetPlayer()->GetGUID();    // player guid
    data << GetSlot();                          // slot (0...2)
    data << GetId();                            // arena team id
    data << stats.rating;                       // rating
    data << stats.games;                        // games
    data << stats.wins;                         // wins
    data << stats.played;                       // played (count of all games, that played...)
    session->SendPacket(&data);
}

void ArenaTeam::SetEmblem(uint32 emblemStyle, uint32 emblemColor, uint32 borderStyle, uint32 borderColor, uint32 backgroundColor)
{
    EmblemStyle = emblemStyle;
    EmblemColor = emblemColor;
    BorderStyle = borderStyle;
    BorderColor = borderColor;
    BackgroundColor = backgroundColor;

    sDatabase.PExecute("UPDATE `arena_team` SET `EmblemStyle`='%u', `EmblemColor`='%u', `BorderStyle`='%u', `BorderColor`='%u', `BackgroundColor`='%u' WHERE `arenateamid`='%u'", EmblemStyle, EmblemColor, BorderStyle, BorderColor, BackgroundColor, Id);
}

void ArenaTeam::SetStats(uint32 stat_type, uint32 value)
{
    switch(stat_type)
    {
        case STAT_TYPE_RATING:
            stats.rating = value;
            sDatabase.PExecute("UPDATE `arena_team_stats` SET `rating` = '%u' WHERE `arenateamid` = '%u'", value, GetId());
            break;
        case STAT_TYPE_GAMES:
            stats.games = value;
            sDatabase.PExecute("UPDATE `arena_team_stats` SET `games` = '%u' WHERE `arenateamid` = '%u'", value, GetId());
            break;
        case STAT_TYPE_WINS:
            stats.wins = value;
            sDatabase.PExecute("UPDATE `arena_team_stats` SET `wins` = '%u' WHERE `arenateamid` = '%u'", value, GetId());
            break;
        case STAT_TYPE_PLAYED:
            stats.played = value;
            sDatabase.PExecute("UPDATE `arena_team_stats` SET `played` = '%u' WHERE `arenateamid` = '%u'", value, GetId());
            break;
        case STAT_TYPE_WINS2:
            stats.wins2 = value;
            sDatabase.PExecute("UPDATE `arena_team_stats` SET `wins2` = '%u' WHERE `arenateamid` = '%u'", value, GetId());
            break;
        case STAT_TYPE_RANK:
            stats.rank = value;
            sDatabase.PExecute("UPDATE `arena_team_stats` SET `rank` = '%u' WHERE `arenateamid` = '%u'", value, GetId());
            break;
        default:
            sLog.outDebug("unknown stat type in ArenaTeam::SetStats() %u", stat_type);
            break;
    }
}

uint8 ArenaTeam::GetSlot()
{
    switch(GetType())
    {
        case ARENA_TEAM_2v2:
            return 0;
        case ARENA_TEAM_3v3:
            return 1;
        case ARENA_TEAM_5v5:
            return 2;
        default:
            sLog.outError("Unknown arena team type %u for arena team %u", GetType(), GetId());
            break;
    }
    return 0xFF;
}

void ArenaTeam::BroadcastPacket(WorldPacket *packet)
{
    MemberList::iterator itr;

    for (itr = members.begin(); itr != members.end(); itr++)
    {
        Player *player = objmgr.GetPlayer(itr->guid);
        if(player)
            player->GetSession()->SendPacket(packet);
    }
}

/*
arenateam fields (id from 2.1.0 client):
1405 - arena team id 2v2
1406 - 0=captain, 1=member?
1407 - played
1408 - unk
1409 - unk
1410 - arena team id 3v3
1411 - 0=captain, 1=member?
1412 - played
1413 - unk
1414 - unk
1415 - arena team id 5v5
1416 - 0=captain, 1=member?
1417 - played
1418 - unk
1419 - unk
*/
