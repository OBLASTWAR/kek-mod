/*	-------------------------------------------------------------------------------------------------------
	© 1991-2012 Take-Two Interactive Software and its subsidiaries.  Developed by Firaxis Games.  
	Sid Meier's Civilization V, Civ, Civilization, 2K Games, Firaxis Games, Take-Two Interactive Software 
	and their respective logos are all trademarks of Take-Two interactive Software, Inc.  
	All other marks and trademarks are the property of their respective owners.  
	All rights reserved. 
	------------------------------------------------------------------------------------------------------- */

#pragma once
#ifndef CVLUASCOPEDNSTANCE_H

#include "CvLuaMethodWrapper.h"

template<class Derived, class InstanceType>
class CvLuaScopedInstance : public CvLuaMethodWrapper<Derived, InstanceType>
{
public:
	static void Push(lua_State* L, InstanceType* pkType);
	static void Push(lua_State* L, FObjectHandle<InstanceType> handle)
	{
		Push(L, handle.pointer());
	}
	static InstanceType* GetInstance(lua_State* L, int idx = 1, bool bErrorOnFail = true);

	//! Used by CvLuaMethodWrapper to know where first argument is.
	static const int GetStartingArgIndex();

	//! KEKMOD: Hooks for Derived to tag instance table t and later verify the tag.
	//! Lua holds raw pointers, so a table kept past its object's deletion points at
	//! freed memory. Derived classes whose objects can die mid-game (CvLuaUnit)
	//! override these so GetInstance rejects stale tables instead of crashing.
	static void PushInstanceTags(lua_State* L, int t, InstanceType* pkType) {}
	static bool IsInstanceTagCurrent(lua_State* L, int t, InstanceType* pkType)
	{
		return true;
	}

protected:
	static void DefaultHandleMissingInstance(lua_State* L);
};



//------------------------------------------------------------------------------
// template members
//------------------------------------------------------------------------------
template<class Derived, class InstanceType>
void CvLuaScopedInstance<Derived, InstanceType>::Push(lua_State* L, InstanceType* pkType)
{
	//Pushing an instance involves more than just actually pushing a pointer into the
	//Lua stack.  There are some caching optimizations that are done as well as some
	//checks.
	//The first step is to load or create a global table <Typename> to store all member
	//methods and all pushed instances.  This conserves memory and offers faster pushing
	//speed.
	//If <Typename>.__instances[pkType] is not nil, return that value.
	//otherwise push a new instance and assign it to __instances.

	//NOTE: Raw gets and sets are used as an optimization over using lua_[get,set]field
	if(pkType)
	{
		//const int t = lua_gettop(L);

		lua_getglobal(L, Derived::GetTypeName());
		if(lua_isnil(L, -1))
		{
			//Typename wasn't found, time to build it.
			lua_pop(L, 1);
			lua_newtable(L);

			//Create weak __instances table.
			lua_pushstring(L, "__instances");
			lua_newtable(L);

			//Create __instances.mt
			lua_newtable(L);
			lua_pushstring(L, "__mode");
			lua_pushstring(L, "v");
			lua_rawset(L, -3);				// mt.__mode = "v";
			lua_setmetatable(L, -2);

			lua_rawset(L, -3);				//type.__instances = t;


			lua_pushvalue(L, -1);
			lua_setglobal(L, Derived::GetTypeName());

			Derived::PushMethods(L, lua_gettop(L));
		}
		const int type_index = lua_gettop(L);

		lua_pushstring(L, "__instances");
		lua_rawget(L, -2);

		const int instances_index = lua_gettop(L);

		lua_pushlightuserdata(L, pkType);

		lua_rawget(L, -2);					//retrieve type.__instances[pkType]

		//KEKMOD: A cached table for this address may belong to a deleted object whose
		//memory was reused. Replace it so old references stay stale.
		if(!lua_isnil(L, -1) && !Derived::IsInstanceTagCurrent(L, lua_gettop(L), pkType))
		{
			lua_pop(L, 1);
			lua_pushnil(L);
		}

		if(lua_isnil(L, -1))
		{
			lua_pop(L, 1);

			//Push new instance
			lua_createtable(L, 0, 1);
			lua_pushlightuserdata(L, pkType);
			lua_setfield(L, -2, "__instance");
			Derived::PushInstanceTags(L, lua_gettop(L), pkType);

			lua_createtable(L, 0, 1);			// create mt
			lua_pushstring(L, "__index");
			lua_pushvalue(L, type_index);
			lua_rawset(L, -3);					// mt.__index = Type
			lua_setmetatable(L, -2);

			//Assign it in instances
			lua_pushlightuserdata(L, pkType);
			lua_pushvalue(L, -2);
			lua_rawset(L, instances_index);				//__instances[pkType] = t;
		}

		//VERIFY(instances_index > type_index);
		lua_remove(L, instances_index);
		lua_remove(L, type_index);

		//const int dt = lua_gettop(L);
		//VERIFY(dt == t + 1)
	}
	else
	{
		lua_pushnil(L);
	}
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType>
InstanceType* CvLuaScopedInstance<Derived, InstanceType>::GetInstance(lua_State* L, int idx, bool bErrorOnFail)
{
	const int stack_size = lua_gettop(L);
	bool bFail = true;
	bool bStale = false;

	//KEKMOD: Make idx absolute; IsInstanceTagCurrent pushes onto the stack.
	if(idx < 0 && idx > LUA_REGISTRYINDEX)
		idx = stack_size + idx + 1;

	InstanceType* pkInstance = NULL;
	if(lua_type(L, idx) == LUA_TTABLE)
	{
		lua_getfield(L, idx, "__instance");
		if(lua_type(L, -1) == LUA_TLIGHTUSERDATA)
		{
			pkInstance = static_cast<InstanceType*>(lua_touserdata(L, -1));
			if(pkInstance)
			{
				//KEKMOD: Object was deleted after this table was handed to Lua.
				if(!Derived::IsInstanceTagCurrent(L, idx, pkInstance))
				{
					pkInstance = NULL;
					bStale = true;
				}
				else
				{
					bFail = false;
				}
			}
		}
	}

	lua_settop(L, stack_size);

	if(bStale && bErrorOnFail)
	{
		Derived::HandleMissingInstance(L);
	}
	else if(bFail && bErrorOnFail)
	{
		if(idx == 1)
			luaL_error(L, "Not a valid instance.  Either the instance is NULL or you used '.' instead of ':'.");
		Derived::HandleMissingInstance(L);
	}
	return pkInstance;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType>
const int CvLuaScopedInstance<Derived, InstanceType>::GetStartingArgIndex()
{
	return 2;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType>
void CvLuaScopedInstance<Derived, InstanceType>::DefaultHandleMissingInstance(lua_State* L)
{
	luaL_error(L, "Instance does not exist.");
}


#endif //CVLUASCOPEDNSTANCE_H