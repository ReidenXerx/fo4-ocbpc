#pragma once

#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>

#include "dirent.h"


#include "Game.h"
// trim from start (in place)
static inline void ltrim(std::string &s)
{
	s.erase(s.begin(), std::find_if(s.begin(), s.end(),
		[](unsigned char c) { return !std::isspace(c); }));
}

// trim from end (in place)
static inline void rtrim(std::string &s)
{
	s.erase(std::find_if(s.rbegin(), s.rend(),
		[](unsigned char c) { return !std::isspace(c); }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s)
{
	ltrim(s);
	rtrim(s);
}

// trim from start (copying)
static inline std::string ltrim_copy(std::string s)
{
	ltrim(s);
	return s;
}

// trim from end (copying)
static inline std::string rtrim_copy(std::string s)
{
	rtrim(s);
	return s;
}

// trim from both ends (copying)
static inline std::string trim_copy(std::string s)
{
	trim(s);
	return s;
}

static inline std::vector<std::string> split(const std::string& s, char delimiter)
{
	std::string str = trim_copy(s);

	std::vector<std::string> tokens;
	if (!str.empty())
	{
		std::string token;
		std::istringstream tokenStream(str);
		while (std::getline(tokenStream, token, delimiter))
		{
			trim(token);
			tokens.emplace_back(token);
		}
	}
	return tokens;
}

static inline void skipComments(std::string& str)
{
	auto pos = str.find("#");
	if (pos != std::string::npos)
	{
		str.erase(pos);
	}
}

static inline std::string gettrimmed(const std::string& s) {
	std::string temp = s;
	if (temp.front() == '[' && temp.back() == ']') {
		temp.erase(0, 1);
		temp.pop_back();
	}
	return temp;
}

static inline std::string CurrentTime()
{
	std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

	char buf[100] = { 0 };
	tm timeStruct;
	errno_t time_good = localtime_s(&timeStruct, &now);
	if (time_good)
		std::strftime(buf, sizeof(buf), "%d-%m-%Y %H:%M:%S", &timeStruct);
	return buf;
}

static inline void NormalizeNiPoint(NiPoint3 &collisionVector, float low, float high)
{
	if (collisionVector.x < low)
		collisionVector.x = low;
	else if (collisionVector.x > high)
		collisionVector.x = high;

	if (collisionVector.y < low)
		collisionVector.y = low;
	else if (collisionVector.y > high)
		collisionVector.y = high;

	if (collisionVector.z < low)
		collisionVector.z = low;
	else if (collisionVector.z > high)
		collisionVector.z = high;
}

static inline float clamp(float val, float min, float max) {
	if (val < min) return min;
	else if (val > max) return max;
	return val;
}

// returns false if different
static inline bool CompareNiPoints(NiPoint3 collisionVector, NiPoint3 emptyPoint)
{
	return collisionVector.x == emptyPoint.x && collisionVector.y == emptyPoint.y && collisionVector.z == emptyPoint.z;
}

static inline BSFixedString ReturnUsableString(std::string str)
{
	BSFixedString fs("");
	fs = str.c_str();
	return fs;
}
/*
static inline void showRotation(NiMatrix33 &r) {
	LOG("%8.2f %8.2f %8.2f", r.data[0][0], r.data[0][1], r.data[0][2]);
	LOG("%8.2f %8.2f %8.2f", r.data[1][0], r.data[1][1], r.data[1][2]);
	LOG("%8.2f %8.2f %8.2f", r.data[2][0], r.data[2][1], r.data[2][2]);
	LOG("-----------------");
}
*/
static inline float distanceNoSqrt2D(NiPoint3 po1, NiPoint3 po2)
{
	/*LARGE_INTEGER startingTime, endingTime, elapsedMicroseconds;
	LARGE_INTEGER frequency;

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&startingTime);
	LOG("distanceNoSqrt2D Start");*/

	float x = po1.x - po2.x;
	float y = po1.y - po2.y;
	float result = x*x + y*y;

	/*QueryPerformanceCounter(&endingTime);
	elapsedMicroseconds.QuadPart = endingTime.QuadPart - startingTime.QuadPart;
	elapsedMicroseconds.QuadPart *= 1000000000LL;
	elapsedMicroseconds.QuadPart /= frequency.QuadPart;
	LOG("distanceNoSqrt2D Update Time = %lld ns\n", elapsedMicroseconds.QuadPart);*/
	return result;
}

static inline float distanceNoSqrt(NiPoint3 po1, NiPoint3 po2)
{
	/*LARGE_INTEGER startingTime, endingTime, elapsedMicroseconds;
	LARGE_INTEGER frequency;

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&startingTime);
	LOG("distanceNoSqrt Start");*/

	float x = po1.x - po2.x;
	float y = po1.y - po2.y;
	float z = po1.z - po2.z;
	float result = x*x + y*y + z*z;

	/*QueryPerformanceCounter(&endingTime);
	elapsedMicroseconds.QuadPart = endingTime.QuadPart - startingTime.QuadPart;
	elapsedMicroseconds.QuadPart *= 1000000000LL;
	elapsedMicroseconds.QuadPart /= frequency.QuadPart;
	LOG("distanceNoSqrt Update Time = %lld ns\n", elapsedMicroseconds.QuadPart);*/
	return result;
}

static inline std::vector<std::string> get_all_files_names_within_folder(std::string folder)
{
	std::vector<std::string> names;

	DIR *directory = opendir(folder.c_str());
	struct dirent *direntStruct;

	if (directory != nullptr) {
		while (direntStruct = readdir(directory)) {
			names.emplace_back(direntStruct->d_name);
		}
	}
	closedir(directory);

	return names;
}

static inline bool stringStartsWith(std::string str, std::string prefix)
{
	// convert string to back to lower case
	std::for_each(str.begin(), str.end(), [](char & c) {
		c = ::tolower(c);
	});
	// std::string::find returns 0 if toMatch is found at starting
	if (str.find(prefix) == 0)
		return true;
	else
		return false;
}

//static inline double GetPercentageValue(double number1, double number2)
//{
//	return number1 + ((number2 - number1)*(50.0 / 100.0f));
//}

