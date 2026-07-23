
#pragma once

#include "CvLuaArgTemplates.h"

template< class Derived, class InstanceType>
class CvLuaMethodWrapper
{

protected:
	//These are helper templates that will allow for quick and easy member function wrapping when
	//implementing a Lua method.
	//They currently do not support non-native types or optional values so the scope is quite limited.
	//regular variations (const)
	// arg types come before FuncClass so callers can write BasicLuaMethod<ret, arg1>(L, &Class::func)
	// and FuncClass is deduced from the function pointer (required for clang; MSVC was more permissive)
	template<typename ret, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)() const);
	template<typename ret, typename arg1, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1) const);
	template<typename ret, typename arg1, typename arg2, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2) const);
	template<typename ret, typename arg1, typename arg2, typename arg3, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2, arg3) const);
	template<typename ret, typename arg1, typename arg2, typename arg3, typename arg4, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2, arg3, arg4) const);

	//regular variations (non const)
	template<typename ret, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)());
	template<typename ret, typename arg1, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1));
	template<typename ret, typename arg1, typename arg2, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2));
	template<typename ret, typename arg1, typename arg2, typename arg3, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2, arg3));
	template<typename ret, typename arg1, typename arg2, typename arg3, typename arg4, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2, arg3, arg4));

	//void variations (const)
	template<typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)() const);
	template<typename arg1, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1) const);
	template<typename arg1, typename arg2, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2) const);
	template<typename arg1, typename arg2, typename arg3, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2, arg3) const);
	template<typename arg1, typename arg2, typename arg3, typename arg4, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2, arg3, arg4) const);

	//void variations (non const)
	template<typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)());
	template<typename arg1, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1));
	template<typename arg1, typename arg2, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2));
	template<typename arg1, typename arg2, typename arg3, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2, arg3));
	template<typename arg1, typename arg2, typename arg3, typename arg4, typename FuncClass>
	static int BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2, arg3, arg4));
};

//------------------------------------------------------------------------------
// template members
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// regular variations (const)
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)() const)
{
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)());

	return 1;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename arg1, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1) const)
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx)));

	return 1;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename arg1, typename arg2, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2) const)
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1)));

	return 1;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename arg1, typename arg2, typename arg3, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2, arg3) const)
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1), CvLuaArgs::toValue<arg3>(L, idx + 2)));

	return 1;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename arg1, typename arg2, typename arg3, typename arg4, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2, arg3, arg4) const)
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1), CvLuaArgs::toValue<arg3>(L, idx + 2), CvLuaArgs::toValue<arg4>(L, idx + 3)));

	return 1;
}

//------------------------------------------------------------------------------
// regular variations (non const)
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)())
{
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)());

	return 1;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename arg1, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1))
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx)));

	return 1;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename arg1, typename arg2, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2))
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1)));

	return 1;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename arg1, typename arg2, typename arg3, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2, arg3))
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1), CvLuaArgs::toValue<arg3>(L, idx + 2)));

	return 1;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename ret, typename arg1, typename arg2, typename arg3, typename arg4, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, ret (FuncClass::*func)(arg1, arg2, arg3, arg4))
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	CvLuaArgs::pushValue<ret>(L, (pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1), CvLuaArgs::toValue<arg3>(L, idx + 2), CvLuaArgs::toValue<arg4>(L, idx + 3)));

	return 1;
}

//------------------------------------------------------------------------------
// void variations (const)
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)() const)
{
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)();
	return 0;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename arg1, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1) const)
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx));
	return 0;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename arg1, typename arg2, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2) const)
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1));
	return 0;
}

//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename arg1, typename arg2, typename arg3, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2, arg3) const)
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1), CvLuaArgs::toValue<arg3>(L, idx + 2));
	return 0;
}

//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename arg1, typename arg2, typename arg3, typename arg4, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2, arg3, arg4) const)
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1), CvLuaArgs::toValue<arg3>(L, idx + 2), CvLuaArgs::toValue<arg4>(L, idx + 3));
	return 0;
}

//------------------------------------------------------------------------------
// void variations (non const)
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)())
{
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)();
	return 0;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename arg1, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1))
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx));
	return 0;
}
//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename arg1, typename arg2, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2))
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1));
	return 0;
}

//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename arg1, typename arg2, typename arg3, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2, arg3))
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1), CvLuaArgs::toValue<arg3>(L, idx + 2));
	return 0;
}

//------------------------------------------------------------------------------
template<class Derived, class InstanceType> template<typename arg1, typename arg2, typename arg3, typename arg4, typename FuncClass>
int CvLuaMethodWrapper<Derived, InstanceType>::BasicLuaMethod(lua_State* L, void (FuncClass::*func)(arg1, arg2, arg3, arg4))
{
	const int idx = Derived::GetStartingArgIndex();
	InstanceType* pkType = Derived::GetInstance(L);
	(pkType->*func)(CvLuaArgs::toValue<arg1>(L, idx), CvLuaArgs::toValue<arg2>(L, idx + 1), CvLuaArgs::toValue<arg3>(L, idx + 2), CvLuaArgs::toValue<arg4>(L, idx + 3));
	return 0;
}
