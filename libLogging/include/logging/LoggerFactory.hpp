/*
 * LoggerFactory.hpp
 *
 *  Created on: 01.03.2013
 *      Author: Patrik Huber
 */
#pragma once

#ifndef LOGGERFACTORY_HPP_
#define LOGGERFACTORY_HPP_

#include "logging/Logger.hpp" // We wouldn't need this include, we could use a forward declaration. But this way, our libs/apps only have to include one file to use the logger.
#include "logging/LogLevels.hpp" // Same as for Logger.hpp.
#include "logging/ConsoleAppender.hpp"

#include <map>
#include <string>
#include <memory>

using std::map;
using std::string;
using std::make_shared;

namespace logging {

#define Loggers LoggerFactory::Instance()

/**
 * Logger factory that manages and exposes different loggers to the user.
 */
class LoggerFactory
{
private:
	/* Private constructor, destructor and copy constructor - we only want one instance of the factory. */
	LoggerFactory();
	~LoggerFactory();
	LoggerFactory(const LoggerFactory &);
	LoggerFactory& operator=(const LoggerFactory &);

public:
	static LoggerFactory* Instance();

	/**
	 * Returns the specified logger. If it is not found, creates a new logger that logs nothing.
	 *
	 * @param[in] name The name of the logger.
	 * @return The specified logger or a new one that logs nothing, if not yet created.
	 */
	Logger& getLogger(const string name);

	/**
	 * Extracts the file basename and returns the logger for the file or a new logger with no logging if it hasn't been created yet.
	 *
	 * @param[in] name The file name or full file path.
	 * @return The specified logger or a new one if not yet created.
	 */
	Logger& getLoggerFor(const string fileName);

	/**
	 * Loads all the loggers and their configuration from a config file. TODO!
	 *
	 * @param[in] filename The name of the config file to load.
	 * @return Maybe something
	 */
	void load(const string filename) {}; // Make static?

private:
	map<string, Logger> loggers;	///< A map of all the loggers and their names.
};

} /* namespace logging */
#endif /* LOGGERFACTORY_HPP_ */
