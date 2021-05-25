#pragma once

#include <vector>
#include <unordered_map>

#include "SomeClass.h"

#include "Generated/SomeOtherClass_hgenerated.h"

template <typename T>
class SomeTemplateClass
{
	T* someT = nullptr;
};

namespace SomeNamespace KGNamespace()
{
	class KGClass() SomeOtherClass
	{
		private:
			KGField(Get[const, &])
			float										_someFloat	= 42.42f; 
			
			KGField(Get[const, &])
			np1::SomeClass								_someClass;

			KGField(Get[const, &])
			SomeTemplateClass<np1::SomeClass>			_someTemplateClass;

			KGField(Get[const, &])
			std::vector<int>							_someVectorOfInt;
			
			KGField(Get[const, &])
			std::vector<np1::SomeClass*>				_someVectorOfSomeClass;

			KGField(Get[const, &])
			std::unordered_map<int, np1::SomeClass*>	_someUmapOfSomeClass2;

		public:
			SomeOtherClass() = default;

		SomeNamespace_SomeOtherClass_GENERATED
	};
}

File_SomeOtherClass_GENERATED