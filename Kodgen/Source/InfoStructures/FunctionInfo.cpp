#include "Kodgen/InfoStructures/FunctionInfo.h"

#include <cassert>
#include <algorithm>

#include "Kodgen/Misc/Helpers.h"

using namespace kodgen;

FunctionInfo::FunctionInfo(CXCursor const& cursor, PropertyGroup&& propertyGroup, EEntityType entityType) noexcept:
	EntityInfo(cursor, std::forward<PropertyGroup>(propertyGroup), entityType),
	isInline{clang_Cursor_isFunctionInlined(cursor) != 0u}
{
	CXType functionType = clang_getCursorType(cursor);

	assert(functionType.kind == CXTypeKind::CXType_FunctionProto);

	//Define prototype
	prototype	= Helpers::getString(clang_getTypeSpelling(functionType));

	//Define return type
	returnType	= TypeInfo(clang_getResultType(functionType));
}

FunctionInfo::FunctionInfo(CXCursor const& cursor, PropertyGroup&& propertyGroup) noexcept:
	FunctionInfo(cursor, std::forward<PropertyGroup>(propertyGroup), EEntityType::Function)
{
	assert(cursor.kind == CXCursorKind::CXCursor_FunctionDecl);

	isStatic = clang_getCursorLinkage(cursor) == CXLinkage_Internal;
}

std::string FunctionInfo::getName() const noexcept
{
	//Remove arguments (...)
	return name.substr(0, name.find_first_of('('));
}

std::string FunctionInfo::getPrototype(bool removeQualifiers, bool removeSpaces) const noexcept
{
	std::string result(prototype);

	//Back is not ')', we have things to remove
	if (removeQualifiers && result.back() != ')')
	{
		size_t removeFrom = result.find_last_of(')');

		assert(removeFrom != std::string::npos);

		result.erase(removeFrom + 1);
	}

	if (removeSpaces)
	{
		result.erase(std::remove(result.begin(), result.end(), ' '), result.end());
	}

	return result;
}