/**
*	Copyright (c) 2020 Julien SOYSOUVANH - All Rights Reserved
*
*	This file is part of the Kodgen library project which is released under the MIT License.
*	See the LICENSE.md file for full license details.
*/

template <typename FileParserType, typename CodeGenUnitType>
void CodeGenManager::processFiles(FileParserType& fileParser, CodeGenUnitType& codeGenUnit, std::set<fs::path> const& toProcessFiles, CodeGenResult& out_genResult) noexcept
{
	std::vector<std::shared_ptr<TaskBase>>	generationTasks;
	std::vector<std::shared_ptr<TaskBase>>	parsingTasks;
	uint8									iterationCount = codeGenUnit.getIterationCount();

	//Reserve enough space for all tasks
	generationTasks.reserve(toProcessFiles.size() * iterationCount);

	//Launch all parsing -> generation processes
	std::shared_ptr<TaskBase> parsingTask;
	std::shared_ptr<TaskBase> preParsingTask;

	const kodgen::MacroCodeGenUnitSettings* codeGenSettings = codeGenUnit.getSettings();
	
	for (int i = 0; i < iterationCount; i++)
	{
		//Lock the thread pool until all tasks have been pushed to avoid competing for the tasks mutex
		_threadPool.setIsRunning(false);

		// First, run pre-parse step in which we will fill generated files
		// with reflection macros (file/class macros).
		// This will avoid the following issue: if we are using inheritance and we haven't
		// generated parent's data while parsing child class we will fail with an error.
		for (fs::path const& file : toProcessFiles)
		{
			auto preParsingTaskLambda = [codeGenSettings, &fileParser, &file](TaskBase*) -> bool
			{
				FileParserType fileParserCopy = fileParser;
				return fileParserCopy.prepareForParsing(file, codeGenSettings);
			};

			preParsingTask = _threadPool.submitTask(std::string("Pre-parsing ") + std::to_string(i), preParsingTaskLambda);
		}

		// Wait for pre-parse step to finish.
		_threadPool.setIsRunning(true);
		_threadPool.joinWorkers();
		_threadPool.setIsRunning(false);

		// Run parsing step.
		for (fs::path const& file : toProcessFiles)
		{
			auto parsingTaskLambda = [codeGenSettings, &fileParser, &file](TaskBase*) -> FileParsingResult
			{
				FileParsingResult	parsingResult;
				
				//Copy a parser for this task
				FileParserType		fileParserCopy = fileParser;

				fileParserCopy.parse(file, parsingResult, codeGenSettings);

				return parsingResult;
			};

			//Add file to the list of parsed files before starting the task to avoid having to synchronize threads
			out_genResult.parsedFiles.push_back(file);
			
			//Parse files
			//For multiple iterations on a same file, the parsing task depends on the previous generation task for the same file
			parsingTask = _threadPool.submitTask(std::string("Parsing ") + std::to_string(i), parsingTaskLambda);
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
		for (fs::path const& file : toProcessFiles)
		{
			// Clear generated file as it will be filled with an actual information.
			// Right now it has some defines that were used for proper parsing.
			const auto generatedFilePath = codeGenSettings->getOutputDirectory() / codeGenSettings->getGeneratedHeaderFileName(file);
			std::ofstream generatedFile(generatedFilePath); // truncate the file
			generatedFile.close();
			
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
			
			generationTasks.emplace_back(_threadPool.submitTask(std::string("Generation ") + std::to_string(i), generationTaskLambda, { parsingTasks[parsingTaskIndex] }));
			parsingTaskIndex += 1;
		}

		// Wait for code generation.
		_threadPool.setIsRunning(true);
		_threadPool.joinWorkers();
		parsingTasks.clear();
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