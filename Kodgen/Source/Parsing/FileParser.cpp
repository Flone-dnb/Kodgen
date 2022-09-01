#include "Kodgen/Parsing/FileParser.h"

#include <cassert>
#include "Kodgen/Misc/Helpers.h"
#include "Kodgen/Misc/DisableWarningMacros.h"
#include "Kodgen/Misc/TomlUtility.h"

using namespace kodgen;

FileParser::FileParser() noexcept:
	_clangIndex{clang_createIndex(0, 0)},
	_settings{std::make_shared<ParsingSettings>()},
	logger{nullptr}
{
}

FileParser::FileParser(FileParser const& other) noexcept:
	NamespaceParser(other),
	_clangIndex{clang_createIndex(0, 0)},	//Don't copy clang index, create a new one
	_settings{other._settings},
	logger{other.logger}
{
}

FileParser::FileParser(FileParser&& other) noexcept:
	NamespaceParser(std::forward<NamespaceParser>(other)),
	_clangIndex{std::forward<CXIndex>(other._clangIndex)},
	_propertyParser(std::forward<PropertyParser>(other._propertyParser)),
	_settings{other._settings},
	logger{other.logger}
{
	other._clangIndex = nullptr;
}

FileParser::~FileParser() noexcept
{
	if (_clangIndex != nullptr)
	{
		clang_disposeIndex(_clangIndex);
	}
}

bool FileParser::populateFileWithMacros(const fs::path& filePath, const std::set<std::string>& macroNamesToDefine)
{
	std::ofstream generatedfile(filePath, std::ios::app);
	if (!generatedfile.is_open())
	{
		return false;
	}

	for (const auto& macroName : macroNamesToDefine)
	{
		generatedfile << "#define " + macroName + " " << std::endl;
	}
	
	generatedfile.close();

	return true;
}

bool FileParser::parse(fs::path const& toParseFile, FileParsingResult& out_result, const kodgen::MacroCodeGenUnitSettings* codeGenSettings) noexcept
{
	assert(_settings.use_count() != 0);

	bool isSuccess = false;

	preParse(toParseFile);

	if (fs::exists(toParseFile) && !fs::is_directory(toParseFile))
	{
		//Fill the parsed file info
		out_result.parsedFile = FilesystemHelpers::sanitizePath(toParseFile);

		// Prepare arrays for errors.
		std::set<std::string> notFoundGeneratedMacroNames;
		std::vector<std::string> errors;

		// Prepare variables for parsing.
		bool rerunParsing = false;
		bool populatedGeneratedFileWithMacros = false;
		CXTranslationUnit translationUnit;

		do
		{
			rerunParsing = false;
			
			// Parse the given file.
			translationUnit = clang_parseTranslationUnit(_clangIndex, toParseFile.string().c_str(), _settings->getCompilationArguments().data(), static_cast<int32>(_settings->getCompilationArguments().size()), nullptr, 0, CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_Incomplete | CXTranslationUnit_KeepGoing);
			if (translationUnit != nullptr)
			{
				errors = getErrors(toParseFile, translationUnit, codeGenSettings, notFoundGeneratedMacroNames);
				if (!populatedGeneratedFileWithMacros && !notFoundGeneratedMacroNames.empty())
				{
					const auto generatedFilePath = codeGenSettings->getOutputDirectory() / codeGenSettings->getGeneratedHeaderFileName(toParseFile);
					if (!populateFileWithMacros(generatedFilePath, notFoundGeneratedMacroNames))
					{
						out_result.errors.emplace_back("Failed to populate the generated file " + generatedFilePath.string() + " with macros.");
						postParse(toParseFile, out_result);
						return false;
					}

					notFoundGeneratedMacroNames.clear();
					populatedGeneratedFileWithMacros = true;
					rerunParsing = true;
				}
				else if (populatedGeneratedFileWithMacros)
				{
					// Clear generated file as it will be filled with an actual information.
					const auto generatedFilePath = codeGenSettings->getOutputDirectory() / codeGenSettings->getGeneratedHeaderFileName(toParseFile);
					std::ofstream file(generatedFilePath); // truncate the file
					file.close();
				}
			}
		}while(rerunParsing);
		
		if (translationUnit != nullptr)
		{
			if (errors.empty())
			{
				ParsingContext& context = pushContext(translationUnit, out_result);

				if (clang_visitChildren(context.rootCursor, &FileParser::parseNestedEntity, this) || !out_result.errors.empty())
				{
					//ERROR
				}
				else
				{
					//Refresh all outer entities contained in the final result
					refreshOuterEntity(out_result);

					isSuccess = true;
				}

				popContext();

				//There should not have any context left once parsing has finished
				assert(contextsStack.empty());
			
				if (_settings->shouldLogDiagnostic)
				{
					logDiagnostic(translationUnit);
				}

				clang_disposeTranslationUnit(translationUnit);
			}
			else
			{
				for (const auto& message : errors)
				{
					out_result.errors.emplace_back(message);
				}
			}
		}
		else
		{
			out_result.errors.emplace_back("Failed to initialize translation unit for file: " + toParseFile.string());
		}
	}
	else
	{
		out_result.errors.emplace_back("File " + toParseFile.string() + " doesn't exist.");
	}

	postParse(toParseFile, out_result);

	return isSuccess;
}

CXChildVisitResult FileParser::parseNestedEntity(CXCursor cursor, CXCursor /* parentCursor */, CXClientData clientData) noexcept
{
	FileParser*	parser	= reinterpret_cast<FileParser*>(clientData);

	DISABLE_WARNING_PUSH
	DISABLE_WARNING_UNSCOPED_ENUM
	
	CXChildVisitResult	visitResult = CXChildVisitResult::CXChildVisit_Continue;

	DISABLE_WARNING_POP

	//Parse the given file ONLY, ignore headers
	if (clang_Location_isFromMainFile(clang_getCursorLocation(cursor)))
	{
		switch (cursor.kind)
		{
			case CXCursorKind::CXCursor_Namespace:
				parser->addNamespaceResult(parser->parseNamespace(cursor, visitResult));
				break;

			case CXCursorKind::CXCursor_StructDecl:
				[[fallthrough]];
			case CXCursorKind::CXCursor_ClassDecl:
				parser->addClassResult(parser->parseClass(cursor, visitResult));
				break;

			case CXCursorKind::CXCursor_ClassTemplate:
				parser->addClassResult(parser->parseClass(cursor, visitResult));
				break;

			case CXCursorKind::CXCursor_EnumDecl:
				parser->addEnumResult(parser->parseEnum(cursor, visitResult));
				break;

			case CXCursorKind::CXCursor_FunctionDecl:
				parser->addFunctionResult(parser->parseFunction(cursor, visitResult));
				break;

			case CXCursorKind::CXCursor_VarDecl:
				parser->addVariableResult(parser->parseVariable(cursor, visitResult));
				break;

			default:
				break;
		}
	}

	return visitResult;
}

ParsingContext& FileParser::pushContext(CXTranslationUnit const& translationUnit, FileParsingResult& out_result) noexcept
{
	_propertyParser.setup(_settings->propertyParsingSettings);

	ParsingContext newContext;

	newContext.parentContext	= nullptr;
	newContext.rootCursor		= clang_getTranslationUnitCursor(translationUnit);
	newContext.propertyParser	= &_propertyParser;
	newContext.parsingSettings	= _settings.get();
	newContext.structClassTree	= &out_result.structClassTree;
	newContext.parsingResult	= &out_result;

	contextsStack.push(std::move(newContext));

	return getContext();
}

void FileParser::addNamespaceResult(NamespaceParsingResult&& result) noexcept
{
	if (result.parsedNamespace.has_value())
	{
		getParsingResult()->namespaces.emplace_back(std::move(result.parsedNamespace).value());
	}

	getParsingResult()->appendResultErrors(result);
}

void FileParser::addClassResult(ClassParsingResult&& result) noexcept
{
	if (result.parsedClass.has_value())
	{
		switch (result.parsedClass->entityType)
		{
			case EEntityType::Struct:
				getParsingResult()->structs.emplace_back(std::move(result.parsedClass).value());
				break;

			case EEntityType::Class:
				getParsingResult()->classes.emplace_back(std::move(result.parsedClass).value());
				break;

			default:
				assert(false);	//Should never reach this line
				break;
		}
	}

	getParsingResult()->appendResultErrors(result);
}

void FileParser::addEnumResult(EnumParsingResult&& result) noexcept
{
	if (result.parsedEnum.has_value())
	{
		getParsingResult()->enums.emplace_back(std::move(result.parsedEnum).value());
	}

	getParsingResult()->appendResultErrors(result);
}

void FileParser::addVariableResult(VariableParsingResult&& result) noexcept
{
	if (result.parsedVariable.has_value())
	{
		getParsingResult()->variables.emplace_back(std::move(result.parsedVariable).value());
	}

	getParsingResult()->appendResultErrors(result);
}

void FileParser::addFunctionResult(FunctionParsingResult&& result) noexcept
{
	if (result.parsedFunction.has_value())
	{
		getParsingResult()->functions.emplace_back(std::move(result.parsedFunction).value());
	}

	getParsingResult()->appendResultErrors(result);
}

void FileParser::refreshOuterEntity(FileParsingResult& out_result) const noexcept
{
	for (NamespaceInfo& namespaceInfo : out_result.namespaces)
	{
		namespaceInfo.refreshOuterEntity();
	}

	for (StructClassInfo& structInfo : out_result.structs)
	{
		structInfo.refreshOuterEntity();
	}

	for (StructClassInfo& classInfo : out_result.classes)
	{
		classInfo.refreshOuterEntity();
	}

	for (EnumInfo& enumInfo : out_result.enums)
	{
		enumInfo.refreshOuterEntity();
	}
}

void FileParser::preParse(fs::path const&) noexcept
{
	/**
	*	Default implementation does nothing special
	*/
}

void FileParser::postParse(fs::path const&, FileParsingResult const&) noexcept
{
	/**
	*	Default implementation does nothing special
	*/
}

std::pair<std::string, std::string> FileParser::splitClassFooterMacroPattern(const std::string& classFooterMacroPattern)
{
	// Get left text.
	const auto leftSharpPos = classFooterMacroPattern.find('#');
	if (leftSharpPos == std::string::npos)
	{
		return std::make_pair("", "");
	}
	std::string leftText;
	if (leftSharpPos != 0)
	{
		leftText = classFooterMacroPattern.substr(0, leftSharpPos);
	}

	// Get right text.
	const auto rightSharpPos = classFooterMacroPattern.rfind('#');
	if (rightSharpPos == std::string::npos)
	{
		return std::make_pair("", "");
	}
	std::string rightText;
	if (rightSharpPos != classFooterMacroPattern.size())
	{
		rightText = classFooterMacroPattern.substr(rightSharpPos + 1);
	}

	return std::make_pair(leftText, rightText);
}

std::vector<std::string> FileParser::getErrors(
	fs::path const& toParseFile,
	CXTranslationUnit const& translationUnit,
	const kodgen::MacroCodeGenUnitSettings* codeGenSettings,
	std::set<std::string>& notFoundGeneratedMacroNames) const noexcept
{
	const CXDiagnosticSet diagnostics = clang_getDiagnosticSetFromTU(translationUnit);
	const unsigned int diagnosticsCount = clang_getNumDiagnosticsInSet(diagnostics);
	const std::string generatedHeaderFilename = codeGenSettings->getGeneratedHeaderFileName(toParseFile).filename().string();
	const std::string fileGeneratedMacroName = codeGenSettings->getHeaderFileFooterMacro(toParseFile);
	const auto [leftClassFooterMacroText, rightClassFooterMacroText] = splitClassFooterMacroPattern(codeGenSettings->getClassFooterMacroPattern());
	if (leftClassFooterMacroText.empty() && rightClassFooterMacroText.empty())
	{
		return {"failed to split class footer macro pattern"};
	}
	const std::string unknownTypeNameError = "unknown type name '";
	
	std::vector<std::string> errors;
	
	for (unsigned i = 0u; i < diagnosticsCount; i++)
	{
		const CXDiagnostic diagnostic(clang_getDiagnosticInSet(diagnostics, i));
		//auto diagnosticMessage = Helpers::getString(clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions()));
		auto diagnosticMessage = Helpers::getString(clang_getDiagnosticSpelling(diagnostic));

		// Look if this error is related to GENERATED macros like "unknown type name 'File_MyClass_GENERATED'".
		if (diagnosticMessage.find(unknownTypeNameError) != std::string::npos)
		{
			const auto unknownTypeName = diagnosticMessage.substr( // don't include last ' character
				unknownTypeNameError.size(), diagnosticMessage.size() - 1 - unknownTypeNameError.size());
			if (unknownTypeName == fileGeneratedMacroName)
			{
				notFoundGeneratedMacroNames.insert(fileGeneratedMacroName);
				continue;
			}
			else if (unknownTypeName.find(leftClassFooterMacroText) != std::string::npos &&
					unknownTypeName.find(rightClassFooterMacroText) != std::string::npos)
			{
				notFoundGeneratedMacroNames.insert(unknownTypeName);
				continue;
			}
		}

		// Prepare location information.
		CXFile file;
		unsigned line, column;
		clang_getExpansionLocation(
			clang_getDiagnosticLocation(diagnostic),
			&file,
			&line,
			&column,
			nullptr);
		auto location = file ? Helpers::getString(clang_getFileName(file)) : std::string();
		location += ", line " + std::to_string(line) + ", column " + std::to_string(column) + "";
		errors.push_back(diagnosticMessage + " (" + location + ")");
		
		clang_disposeDiagnostic(diagnostic);
	}

	clang_disposeDiagnosticSet(diagnostics);

	return errors;
}

bool FileParser::logDiagnostic(CXTranslationUnit const& translationUnit) const noexcept
{
	if (logger != nullptr)
	{
		CXDiagnosticSet diagnostics = clang_getDiagnosticSetFromTU(translationUnit);

		unsigned int diagnosticsCount = clang_getNumDiagnosticsInSet(diagnostics);

		//Log only if there is at least 1 diagnostic entry
		if (diagnosticsCount > 0)
		{
			logger->log("Start diagnostic...", ILogger::ELogSeverity::Info);

			for (unsigned i = 0u; i < diagnosticsCount; i++)
			{
				CXDiagnostic diagnostic(clang_getDiagnosticInSet(diagnostics, i));

				logger->log(Helpers::getString(clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions())), ILogger::ELogSeverity::Warning);

				clang_disposeDiagnostic(diagnostic);
			}

			logger->log("End diagnostic...", ILogger::ELogSeverity::Info);
		}

		clang_disposeDiagnosticSet(diagnostics);

		return true;
	}
	else
	{
		return false;
	}
}