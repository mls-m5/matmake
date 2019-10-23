// Copyright Mattias Larsson Sköld

#pragma once

#include "dependency.h"
#include "buildtarget.h"
#include <fstream>

class CopyFile: public Dependency {
public:
	CopyFile(const CopyFile &) = delete;
	CopyFile(CopyFile &&) = delete;
	CopyFile(Token source, Token output, BuildTarget *parent, IEnvironment *env):
		  Dependency(env),
		  source(source),
		  output(output),
		  parent(parent) {}

	Token source;
	Token output;
	BuildTarget *parent;

	time_t getSourceChangedTime() {
		return env().fileHandler().getTimeChanged(source);
	}

	time_t build() override {
		if (output == source) {
			vout << "file " << output << " source and target is on same place. skipping" << endl;
		}
		auto timeChanged = getTimeChanged();

		if (getSourceChangedTime() > timeChanged) {
			queue(true);
			dirty(true);

			return time(nullptr);
		}
		return timeChanged;
	}

	void work() override {
		ifstream src(source);
		if (!src.is_open()) {
			cout << "could not open file " << source << " for copy for target " << parent->name << endl;
		}

		ofstream dst(output);
		if (!dst) {
			cout << "could not open file " << output << " for copy for target " << parent->name << endl;
		}

		vout << "copy " << source << " --> " << output << endl;
		dst << src.rdbuf();

		sendSubscribersNotice();
	}

	void clean() override {
		vout << "removing file " << output << endl;
		remove(output.c_str());
	}

	Token targetPath() override {
		return output;
	}

	bool includeInBinary() override {
		return false;
	}
};
