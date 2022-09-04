/**
*	Copyright (c) 2020 Julien SOYSOUVANH - All Rights Reserved
*
*	This file is part of the Kodgen library project which is released under the MIT License.
*	See the LICENSE.md file for full license details.
*/

#pragma once

#include <string>
#include <vector>
#include <set>
#include <memory>	//std::shared_ptr

#include <clang-c/Index.h>

#include "Kodgen/Parsing/NamespaceParser.h"
#include "Kodgen/Parsing/ParsingResults/FileParsingResult.h"
#include "Kodgen/Parsing/ParsingSettings.h"
#include "Kodgen/Parsing/PropertyParser.h"
#include "Kodgen/Misc/Filesystem.h"
#include "Kodgen/Misc/ILogger.h"
#include <Kodgen/CodeGen/Macro/MacroCodeGenUnitSettings.h>

namespace kodgen
{
	class FileParser : public NamespaceParser
	{
		private:
			/** Index used internally by libclang to process a translation unit. */
			CXIndex								_clangIndex;

			/** Property parser used to parse properties of all entities. */
			PropertyParser						_propertyParser;		

			/** Settings to use during parsing. */
			std::shared_ptr<ParsingSettings>	_settings;

			/**
			*	@brief This method is called at each node (cursor) of the parsing.
			*
			*	@param cursor		Current cursor to parse.
			*	@param parentCursor	Parent of the current cursor.
			*	@param clientData	Pointer to a data provided by the client. Must contain a FileParser*.
			*
			*	@return An enum which indicates how to choose the next cursor to parse in the AST.
			*/
			static CXChildVisitResult	parseNestedEntity(CXCursor		cursor,
														  CXCursor		parentCursor,
														  CXClientData	clientData)						noexcept;

			/**
			*	@brief Push a new clean context to prepare translation unit parsing.
			*
			*	@param translationUnit	The translation unit to parse.
			*	@param out_result		Result to fill during parsing.
			*
			*	@return The new context.
			*/
			ParsingContext&				pushContext(CXTranslationUnit const&	translationUnit,
													FileParsingResult&			out_result)				noexcept;

			/**
			*	@brief Add the provided namespace result to the current file context result.
			*
			*	@param result NamespaceParsingResult to add.
			*/
			void						addNamespaceResult(NamespaceParsingResult&& result)				noexcept;

			/**
			*	@brief Add the provided struct/class result to the current file context result.
			*
			*	@param result ClassParsingResult to add.
			*/
			void						addClassResult(ClassParsingResult&& result)						noexcept;

			/**
			*	@brief Add the provided enum result to the current file context result.
			*
			*	@param result EnumParsingResult to add.
			*/
			void						addEnumResult(EnumParsingResult&& result)						noexcept;

			/**
			*	@brief Add the provided variable result to the current file context result.
			*
			*	@param result VariableParsingResult to add.
			*/
			void						addVariableResult(VariableParsingResult&& result)				noexcept;

			/**
			*	@brief Add the provided function result to the current file context result.
			*
			*	@param result FunctionParsingResult to add.
			*/
			void						addFunctionResult(FunctionParsingResult&& result)				noexcept;
			
			/**
			*	@brief Refresh outer entities of the passed FileParsingResult.
			*
			*	@param out_result Result to refresh.
			*/
			void						refreshOuterEntity(FileParsingResult& out_result)		const	noexcept;

			/**
			*	@brief Log the diagnostic of the provided translation unit.
			*
			*	@param translationUnit Translation unit we want to log the diagnostic of.
			* 
			*	@return true if the diagnostic could be logged, else false (logger is nullptr).
			*/
			bool						logDiagnostic(CXTranslationUnit const& translationUnit)	const	noexcept;

			/**
			*	Splits macro pattern into two parts between "##...##" substring. For example:
			*	for "##CLASSFULLNAME##_GENERATED" it will return a pair of "" (empty) and "_GENERATED".
			*
			*	@param macroPattern Macro pattern.
			*
			*	@return a pair of strings between "##...##" substring.
			*/
			static std::pair<std::string, std::string> splitMacroPattern(const std::string& macroPattern);

			/**
			*	Clears the file and adds "#define" statements for the specified macros.
			*
			*	@param filePath           File in which to define macros.
			*	@param macroNamesToDefine Macro names to define.
			*
			*	@retun true if successfull, false is failed.
			*/
			static bool populateFileWithMacros(const fs::path& filePath, const std::set<std::string>& macroNamesToDefine);
		
			/**
			*	@brief Returns all errors found during translation unit parsing.
			*
			*   @parma toParseFile					File that was used in parsing.
			*	@param translationUnit				Translation unit we want to check.
			*	@param codeGenSettings				Code generation settings that was used.
			*	@param notFoundGeneratedMacroNames  Set of GENERATED macro names that were not found
			*	during parsing.
			*
			*	@remark This function ignores errors caused by including generated headers or
			*	using GENERATED or reflection macros.
			*
			*	@return array of errors (if found).
			*/
			std::vector<std::string> getErrors(
				fs::path const& toParseFile,
				CXTranslationUnit const& translationUnit,
				const kodgen::MacroCodeGenUnitSettings* codeGenSettings,
				std::set<std::string>& notFoundGeneratedMacroNames) const	noexcept;

			/**
			*	@brief Helper to get the ParsingResult contained in the context as a FileParsingResult.
			*
			*	@return The cast FileParsingResult.
			*/
			inline FileParsingResult*	getParsingResult()												noexcept;

		protected:
			/**
			*	@brief Overridable method called just before starting the parsing process of a file
			*
			*	@param parseFile Path to the file which is about to be parsed.
			*/
			virtual void preParse(fs::path const& parseFile)	noexcept;

			/**
			*	@brief Overridable method called just after the parsing process has been finished
			*	@brief Even if the parsing process ended prematurely, this method is called
			*
			*	@param parseFile Path to the file which has been parsed
			*	@param result Result of the parsing
			*/
			virtual void postParse(fs::path const& parseFile, FileParsingResult const& result)	noexcept;

		public:
			/** Logger used to issue logs from the FileParser. Can be nullptr. */
			ILogger*			logger	= nullptr;

			FileParser()					noexcept;
			FileParser(FileParser const&)	noexcept;
			FileParser(FileParser&&)		noexcept;
			virtual ~FileParser()			noexcept;

			/**
			*	@brief Prepares initial generated file for parsing before calling to @ref parseFailOnErrors.
			*
			*	@param toParseFile	   Path to the file to parse.
			*	@param codeGenSettings Code generation settings.
			*
			*	@return true if the parsing process finished without error, else false
			*/
			bool					prepareForParsing(fs::path const&					      toParseFile,
													  const kodgen::MacroCodeGenUnitSettings* codeGenSettings)		noexcept;

			/**
			*	@brief Parse the file and fill the FileParsingResult while ignoring any parsing errors.
			*
			*	@param toParseFile	   Path to the file to parse.
			*	@param out_result	   Result filled while parsing the file.
			*
			*	@return true if the parsing process finished without error, else false
			*/
			bool					parseIgnoreErrors(fs::path const&						  toParseFile,
													  FileParsingResult&				      out_result)		noexcept;

			/**
			*	@brief Parse the file and fill the FileParsingResult while failing on any parsing errors.
			*
			*	@param toParseFile	   Path to the file to parse.
			*	@param out_result	   Result filled while parsing the file.
			*	@param codeGenSettings Code generation settings.
			*
			*	@return true if the parsing process finished without error, else false
			*/
			bool					parseFailOnErrors(fs::path const&						  toParseFile,
													  FileParsingResult&				      out_result,
													  const kodgen::MacroCodeGenUnitSettings* codeGenSettings)		noexcept;

			/**
			*	@brief Getter for _settings field.
			* 
			*	@return _settings.
			*/
			inline ParsingSettings&	getSettings()											noexcept;
	};

	#include "Kodgen/Parsing/FileParser.inl"
}