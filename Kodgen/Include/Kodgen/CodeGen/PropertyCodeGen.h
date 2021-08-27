/**
*	Copyright (c) 2020 Julien SOYSOUVANH - All Rights Reserved
*
*	This file is part of the Kodgen library project which is released under the MIT License.
*	See the README.md file for full license details.
*/

#pragma once

#include <string>

#include "Kodgen/Config.h"
#include "Kodgen/CodeGen/CodeGenEnv.h"
#include "Kodgen/CodeGen/ICodeGenerator.h"
#include "Kodgen/Misc/FundamentalTypes.h"
#include "Kodgen/InfoStructures/EntityInfo.h"

namespace kodgen
{
	class PropertyCodeGen : public ICodeGenerator
	{
		private:
			struct AdditionalData
			{
				uint8			propertyIndex;
				Property const* property;
			};

			/** Mask defining the type of entities this generator can run on. */
			EEntityType	_eligibleEntityMask = EEntityType::Undefined;

			/**
			*	@brief	Call the visitor method once for each entity/property pair.
			*			The forwarded data is a PropertyCodeGen::AdditionalData const*.
			* 
			*	@param entity	The entity provided to the visitor.
			*	@param env		The environment provided to the visitor.
			*	@param visitor	The visitor to run.
			* 
			*	@return	AbortWithFailure if any of the visitor calls returned AbortWithFailure;
			*			Recurse if the entity can contain entities overlapping with the _eligibleEntityMask;
			*			Continue if the entity doesn't contain any entities overlapping with the _eligibleEntityMask;
			*/
			virtual ETraversalBehaviour	callVisitorOnEntity(EntityInfo const&									entity,
															CodeGenEnv&											env,
															std::function<ETraversalBehaviour(ICodeGenerator&,
																							  EntityInfo const&,
																							  CodeGenEnv&,
																							  void const*)>		visitor)	noexcept final override;

			/**
			*	@brief	Generate code for the provided entity/environment pair.
			*			Internally call the PropertyCodeGen::generateCode public method by unwrapping the data content.
			* 
			*	@param entity		The entity this generator should generate code for.
			*	@param env			The generation environment structure.
			*	@param inout_result	String the generated code should be appended to.
			*	@param data			PropertyCodeGen::AdditionalData const* forwarded from PropertyCodeGen::callVisitorOnEntity.
			* 
			*	@return A ETraversalBehaviour defining how the CodeGenUnit should pick the next entity.
			*/
			virtual ETraversalBehaviour	generateCode(EntityInfo const&	entity, 
													 CodeGenEnv&		env,
													 std::string&		inout_result,
													 void const*		data)												noexcept final override;

			/**
			*	@brief	Determine whether this PropertyCodeGen should recurse on the provided entity children or not.
			* 
			*	@param entity The entity to check.
			* 
			*	@return true if the generator should run on the entity's children, else false.
			*/
			bool						shouldIterateOnNestedEntities(EntityInfo const& entity)						const	noexcept;

		protected:
			/**
			*	@brief Check if 2 entity types masks have at least one common entity type.
			* 
			*	@param lhs First entity type mask.
			*	@param rhs Second entity type mask.
			* 
			*	@return true if at least one entity type is common to the 2 provided masks, else false.
			*/
			static inline bool entityTypeOverlap(EEntityType lhs, EEntityType rhs)	noexcept;

		public:
			/**
			*	@param eligibleEntityMask A mask defining all the types of entity this PropertyCodeGen instance should run on.
			*/
			PropertyCodeGen(EEntityType eligibleEntityMask)	noexcept;

			/**
			*	@brief Generate code for a given entity.
			*	
			*	@param entity			Entity to generate code for.
			*	@param property			Property that triggered the property generation.
			*	@param propertyIndex	Index of the property in the entity's propertyGroup.
			*	@param env				Generation environment structure.
			*	@param inout_result		String the method should append the generated code to.
			*	
			*	@return true if the generation completed successfully, else false.
			*/
			virtual bool	generateCode(EntityInfo const&	entity,
										 Property const&	property,
										 uint8				propertyIndex,
										 CodeGenEnv&		env,
										 std::string&		inout_result)					noexcept = 0;

			/**
			*	@brief Check if this property should generate code for the provided entity/property pair.
			*
			*	@param entity			Checked entity.
			*	@param property			Checked property.
			*	@param propertyIndex	Index of the property in the entity's propertyGroup.
			*
			*	@return true if this property should generate code for the provided entity, else false.
			*/
			virtual bool	shouldGenerateCode(EntityInfo const&	entity,
											   Property const&		property,
											   uint8				propertyIndex)	const	noexcept = 0;

			/**
			*	@brief	Initialize the property code gen with the provided environment.
			*			The method is called by CodeGenModule::initialize before any call to PropertyCodeGen::generateCode.
			*			This default implementation does nothing and returns true by default.
			*
			*	@param env Generation environment.
			* 
			*	@return true if the initialization completed successfully, else false.
			*/
			virtual bool	initialize(CodeGenEnv& env)										noexcept;
	};

	#include "Kodgen/CodeGen/PropertyCodeGen.inl"
}