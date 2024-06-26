#include "Kodgen/Threading/TaskBase.h"

using namespace kodgen;

TaskBase::TaskBase(char const* name, std::vector<std::shared_ptr<TaskBase>>&& deps) noexcept:
	_name{name},
	dependencies{std::forward<std::vector<std::shared_ptr<TaskBase>>>(deps)}
{
}

std::string const& TaskBase::getName() const noexcept
{
	return _name;
}