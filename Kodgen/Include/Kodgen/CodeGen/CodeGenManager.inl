/**
*	Copyright (c) 2020 Julien SOYSOUVANH - All Rights Reserved
*
*	This file is part of the Kodgen library project which is released under the MIT License.
*	See the LICENSE.md file for full license details.
*/

template <typename FileParserType, typename CodeGenUnitType>
void CodeGenManager::processFiles(FileParserType& fileParser, CodeGenUnitType& codeGenUnit, std::set<fs::path> const& toProcessFiles, CodeGenResult& out_genResult) noexcept
{
	ParsingSettings parsingSettings = fileParser.getSettings();
	if (!parsingSettings.shouldFailCodeGenerationOnClangErrors)
	{
		processFilesIgnoreErrors(fileParser, codeGenUnit, toProcessFiles, out_genResult);
	}
	else
	{
		processFilesFailOnErrors(fileParser, codeGenUnit, toProcessFiles, out_genResult);
	}
}

template <typename FileParserType, typename CodeGenUnitType>
void CodeGenManager::processFilesFailOnErrors(FileParserType& fileParser, CodeGenUnitType& codeGenUnit, std::set<fs::path> const& toProcessFiles, CodeGenResult& out_genResult) noexcept
{
	std::vector<std::shared_ptr<TaskBase>>	generationTasks;

	//Reserve enough space for all tasks
	generationTasks.reserve(toProcessFiles.size());

	//Launch all parsing -> generation processes
	std::shared_ptr<TaskBase> parsingTask;
	std::shared_ptr<TaskBase> preParsingTask;

	const kodgen::MacroCodeGenUnitSettings* codeGenSettings = codeGenUnit.getSettings();
	std::set<fs::path> filesLeftToProcess = toProcessFiles;
	std::vector<std::pair<fs::path, ParsingError>> parsingResultsOfFailedFiles;
	size_t filesLeftBefore = 0;
	
	// Process files in cycle.
	// Files that failed parsing step will be queued for the next cycle iteration to be parsed again.
	// This is needed because sometimes not all GENERATED macros are filled on pre-parsing step (usually,
	// when we have an include chain of multiple files that use reflection some GENERATED macros
	// are not detected on pre-parsing step).
	do
	{
		parsingResultsOfFailedFiles.clear();
		filesLeftBefore = filesLeftToProcess.size();
		std::set<fs::path> filesToProcessThisIteration = std::move(filesLeftToProcess);
		
		//Lock the thread pool until all tasks have been pushed to avoid competing for the tasks mutex
		_threadPool.setIsRunning(false);

		// First, run pre-parse step in which we will fill generated files
		// with reflection macros (file/class macros).
		// This will avoid the following issue: if we are using inheritance and we haven't
		// generated parent's macros while parsing child class we will fail with an error.
		std::vector<std::set<std::string>> fileMacrosToDefine(filesToProcessThisIteration.size());
		size_t iPreParsingFileIndex = 0;
		for (fs::path const& file : filesToProcessThisIteration)
		{
			std::set<std::string>* macrosToDefine = &fileMacrosToDefine[iPreParsingFileIndex];
			auto preParsingTaskLambda = [codeGenSettings, &fileParser, &file, macrosToDefine](TaskBase*) -> bool
			{
				FileParserType fileParserCopy = fileParser;
				return fileParserCopy.prepareForParsing(file, codeGenSettings, *macrosToDefine);
			};

			preParsingTask = _threadPool.submitTask(std::string("Pre-parsing ") + file.string(), preParsingTaskLambda);

			iPreParsingFileIndex += 1;
		}

		// Wait for pre-parse step to finish.
		_threadPool.setIsRunning(true);
		_threadPool.joinWorkers();
		_threadPool.setIsRunning(false);

		// Define generated macros.
		iPreParsingFileIndex = 0;
		for (fs::path const& file : filesToProcessThisIteration)
		{
			if (!fileMacrosToDefine[iPreParsingFileIndex].empty())
			{
				// Populate generated file with macros.
				const auto generatedFilePath = codeGenSettings->getOutputDirectory() / codeGenSettings->getGeneratedHeaderFileName(file);

				std::ofstream generatedfile(generatedFilePath, std::ios::app);
				for (const auto& macroName : fileMacrosToDefine[iPreParsingFileIndex])
				{
					generatedfile << "#define " + macroName + " " << std::endl;
				}
				generatedfile.close();
			}

			iPreParsingFileIndex += 1;
		}
		
		// Run parsing step.
		std::vector<std::shared_ptr<TaskBase>> parsingTasks;
		for (fs::path const& file : filesToProcessThisIteration)
		{
			auto parsingTaskLambda = [codeGenSettings, &fileParser, &file, &filesLeftToProcess, &parsingResultsOfFailedFiles](TaskBase*) -> FileParsingResult
			{
				FileParsingResult	parsingResult;
				
				// Copy a parser for this task.
				FileParserType		fileParserCopy = fileParser;

				fileParserCopy.parseFailOnErrors(file, parsingResult, codeGenSettings);
				if (!parsingResult.errors.empty())
				{
					for (const auto& error : parsingResult.errors)
					{
						parsingResultsOfFailedFiles.push_back(std::make_pair(file, error));
					}
					parsingResult.errors.clear();
					filesLeftToProcess.insert(file);
				}

				return parsingResult;
			};

			//Add file to the list of parsed files before starting the task to avoid having to synchronize threads
			out_genResult.parsedFiles.push_back(file);
			
			parsingTask = _threadPool.submitTask(std::string("Parsing ") + file.string(), parsingTaskLambda);
			parsingTasks.push_back(parsingTask);
		}

		// Wait for parse step to finish.
		// We wait here for all tasks to be finished because on the next step
		// (the generation step) we will fill generated files with an actual data
		// and this will clear existing macro defines that generated files have,
		// but we need these macros for all parsing tasks to be finished without errors.
		_threadPool.setIsRunning(true);
		_threadPool.joinWorkers();
		_threadPool.setIsRunning(false);

		// Run code generation after all files were parsed.
		size_t parsingTaskIndex = 0;
		for (fs::path const& file : filesToProcessThisIteration)
		{
			// Check if the parsing step failed for this file.
			bool bParsingFailed = false;
			for (const auto& failedFile : filesLeftToProcess)
			{
				if (failedFile == file)
				{
					bParsingFailed = true;
					break;
				}
			}
			if (bParsingFailed)
			{
				parsingTaskIndex += 1;
				continue;
			}
			
			// Clear generated file as it will be filled with an actual information.
			// Right now it has some defines that were used for proper parsing.
			const auto generatedFilePath = codeGenSettings->getOutputDirectory() / codeGenSettings->getGeneratedHeaderFileName(file);
			std::ofstream generatedFile(generatedFilePath); // truncate the file
			generatedFile.close();
			
			auto generationTaskLambda = [&codeGenUnit](TaskBase* parsingTask) -> CodeGenResult
			{
				CodeGenResult out_generationResult;

				// Copy the generation unit model to have a fresh one for this generation unit.
				CodeGenUnitType	generationUnit = codeGenUnit;

				// Get the result of the parsing task.
				FileParsingResult parsingResult = TaskHelper::getDependencyResult<FileParsingResult>(parsingTask, 0u);

				// Generate the file if no errors occured during parsing.
				if (parsingResult.errors.empty())
				{
					out_generationResult.completed = generationUnit.generateCode(parsingResult);
				}

				return out_generationResult;
			};
			
			generationTasks.emplace_back(_threadPool.submitTask(std::string("Generation ") + file.string(), generationTaskLambda, { parsingTasks[parsingTaskIndex] }));
			parsingTaskIndex += 1;
		}

		// Wait for code generation.
		_threadPool.setIsRunning(true);
		_threadPool.joinWorkers();
	}while(!filesLeftToProcess.empty() && filesLeftBefore != filesLeftToProcess.size());

	// Log errors.
	if (logger != nullptr)
	{
		if (!parsingResultsOfFailedFiles.empty())
		{
			out_genResult.completed = false;
		}
		
		for (const auto error : parsingResultsOfFailedFiles)
		{
			logger->log("While processing the following file: " + error.first.string() + ": " + error.second.toString(), kodgen::ILogger::ELogSeverity::Error);
		}
	}
	
	//Merge all generation results together
	for (std::shared_ptr<TaskBase>& task : generationTasks)
	{
		out_genResult.mergeResult(TaskHelper::getResult<CodeGenResult>(task.get()));
	}
}

template <typename FileParserType, typename CodeGenUnitType>
void CodeGenManager::processFilesIgnoreErrors(FileParserType& fileParser, CodeGenUnitType& codeGenUnit, std::set<fs::path> const& toProcessFiles, CodeGenResult& out_genResult) noexcept
{
	std::vector<std::shared_ptr<TaskBase>>	generationTasks;
	uint8									iterationCount = codeGenUnit.getIterationCount();

	//Reserve enough space for all tasks
	generationTasks.reserve(toProcessFiles.size() * iterationCount);

	//Launch all parsing -> generation processes
	std::shared_ptr<TaskBase> parsingTask;
	
	for (int i = 0; i < iterationCount; i++)
	{
		//Lock the thread pool until all tasks have been pushed to avoid competing for the tasks mutex
		_threadPool.setIsRunning(false);

		for (fs::path const& file : toProcessFiles)
		{
			auto parsingTaskLambda = [&fileParser, &file](TaskBase*) -> FileParsingResult
			{
				//Copy a parser for this task
				FileParserType		fileParserCopy = fileParser;
				FileParsingResult	parsingResult;

				fileParserCopy.parseIgnoreErrors(file, parsingResult);

				return parsingResult;
			};

			auto generationTaskLambda = [&codeGenUnit](TaskBase* parsingTask) -> CodeGenResult
			{
				CodeGenResult out_generationResult;

				//Copy the generation unit model to have a fresh one for this generation unit
				CodeGenUnitType	generationUnit = codeGenUnit;

				//Get the result of the parsing task
				FileParsingResult parsingResult = TaskHelper::getDependencyResult<FileParsingResult>(parsingTask, 0u);

				//Generate the file if no errors occured during parsing
				if (parsingResult.errors.empty())
				{
					out_generationResult.completed = generationUnit.generateCode(parsingResult);
				}

				return out_generationResult;
			};

			//Add file to the list of parsed files before starting the task to avoid having to synchronize threads
			out_genResult.parsedFiles.push_back(file);

			//Parse files
			//For multiple iterations on a same file, the parsing task depends on the previous generation task for the same file
			parsingTask = _threadPool.submitTask(std::string("Parsing ") + std::to_string(i), parsingTaskLambda);

			//Generate code
			generationTasks.emplace_back(_threadPool.submitTask(std::string("Generation ") + std::to_string(i), generationTaskLambda, { parsingTask }));
		}

		//Wait for this iteration to complete before continuing any further
		//(an iteration N depends on the iteration N - 1)
		_threadPool.setIsRunning(true);
		_threadPool.joinWorkers();
	}

	//Merge all generation results together
	for (std::shared_ptr<TaskBase>& task : generationTasks)
	{
		out_genResult.mergeResult(TaskHelper::getResult<CodeGenResult>(task.get()));
	}
}

template <typename FileParserType, typename CodeGenUnitType>
CodeGenResult CodeGenManager::run(FileParserType& fileParser, CodeGenUnitType& codeGenUnit, bool forceRegenerateAll) noexcept
{
	//Check FileParser validity
	static_assert(std::is_base_of_v<FileParser, FileParserType>, "fileParser type must be a derived class of kodgen::FileParser.");
	static_assert(std::is_copy_constructible_v<FileParserType>, "The provided file parser must be copy-constructible.");

	//Check FileGenerationUnit validity
	static_assert(std::is_base_of_v<CodeGenUnit, CodeGenUnitType>, "codeGenUnit type must be a derived class of kodgen::CodeGenUnit.");
	static_assert(std::is_copy_constructible_v<CodeGenUnitType>, "The CodeGenUnit you provide must be copy-constructible.");

	CodeGenResult genResult;
	genResult.completed = true;

	if (!checkGenerationSetup(fileParser, codeGenUnit))
	{
		genResult.completed = false;
	}
	else
	{
		//Start timer here
		auto				start			= std::chrono::high_resolution_clock::now();
		std::set<fs::path>	filesToProcess	= identifyFilesToProcess(codeGenUnit, genResult, forceRegenerateAll);

		//Don't setup anything if there are no files to generate
		if (filesToProcess.size() > 0u)
		{
			//Initialize the parsing settings to setup parser compilation arguments.
			//parsingSettings can't be nullptr since it has been checked in the checkGenerationSetup call.
			fileParser.getSettings().init(logger);

			generateMacrosFile(fileParser.getSettings(), codeGenUnit.getSettings()->getOutputDirectory());

			//Start files processing
			processFiles(fileParser, codeGenUnit, filesToProcess, genResult);
		}

		genResult.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() * 0.001f;
	}
	
	return genResult;
}