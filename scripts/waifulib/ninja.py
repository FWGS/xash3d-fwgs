#!/usr/bin/env python
# encoding: utf-8
# Copyright (C) 2025 Velaron
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import io
import os
import abc

try:
	from cStringIO import StringIO
except ImportError:
	from io import StringIO

from abc import abstractmethod

try:
	from typing import List, Type, Union
except ImportError:
	pass

from waflib import Logs, Task, Build, Options, Node
from ninja_syntax import Writer, escape_path

try:
	ABC = abc.ABC
except AttributeError:
	class ABC(object):
		__metaclass__ = abc.ABCMeta

Task.Task.keep_last_cmd = True


def get_node_path(node):  # type: (Union[Node, str]) -> str
	if isinstance(node, str):
		return escape_path(node)
	else:
		return escape_path(node.abspath())


class TaskAdapter(ABC):
	task_type = None  # type: str

	def __init__(self, task):  # type: (Task) -> None
		if not task.inputs:
			raise ValueError()

		if not task.outputs:
			raise ValueError()

		self.task = task  # type: Task

	@classmethod
	def write_rule(cls, writer):  # type: (Writer) -> None
		writer.rule(
			name=cls.task_type,
			command="$cmd",
			description="Building {} object $out".format(cls.task_type)
		)
		writer.newline()

	@abstractmethod
	def write_build(self, writer):  # type: (Writer) -> None
		pass

	def write_target(self, writer):  # type: (Writer) -> None
		pass

	def get_inputs(self):  # type: () -> List[str]
		return []

	def get_outputs(self):  # type: () -> List[str]
		return []


class CAdapter(TaskAdapter):
	task_type = "c"  # type: str

	def write_build(self, writer):  # type: (Writer) -> None
		output_file = self.task.outputs[0].path_from(self.task.generator.bld.bldnode)
		input_files = []

		cmd = " ".join(self.task.last_cmd)

		for node in self.task.inputs:
			cmd = cmd.replace(node.path_from(self.task.get_cwd()), node.abspath())
			input_files.append(node.abspath())

		cmd = cmd.replace(self.task.outputs[0].abspath(), output_file)

		for inc in self.task.env.INCPATHS:
			cwd = self.task.get_cwd().abspath()
			path = os.path.normpath(os.path.join(cwd, inc))
			cmd = cmd.replace(self.task.env.CPPPATH_ST % inc,
							  self.task.env.CPPPATH_ST % path)

		writer.build(
			rule=self.task_type,
			outputs=output_file,
			inputs=input_files,
			variables={
				"cmd": cmd,
			})
		writer.newline()


class CxxAdapter(CAdapter):
	task_type = "cxx"  # type: str


class CStLibAdapter(TaskAdapter):
	task_type = "cstlib"  # type: str

	def write_build(self, writer):  # type: (Writer) -> None
		output_file = self.task.outputs[0].path_from(self.task.generator.bld.bldnode)
		input_files = []

		cmd = " ".join(self.task.last_cmd)

		for node in self.task.inputs:
			cmd = cmd.replace(node.path_from(self.task.get_cwd()), node.abspath())
			input_files.append(node.abspath())

		cmd = cmd.replace(self.task.outputs[0].abspath(), output_file)

		writer.build(
			rule=self.task_type,
			outputs=output_file,
			inputs=input_files,
			variables={
				"cmd": cmd,
			})
		writer.newline()

	def write_target(self, writer):  # type: (Writer) -> None
		writer.build(
			rule="waf_build",
			outputs="{}.passthrough".format(self.get_outputs()[0]),
			inputs=self.get_inputs(),
			variables={
				"tgt": self.task.generator.name
			})
		writer.newline()

	def get_inputs(self):  # type: () -> List[str]
		return [node.abspath() for node in self.task.generator.source]

	def get_outputs(self):  # type: () -> List[str]
		return [self.task.outputs[0].path_from(self.task.generator.bld.bldnode)]


class CShLibAdapter(CStLibAdapter):
	task_type = "cshlib"  # type: str

	def write_build(self, writer):  # type: (Writer) -> None
		output_file = self.task.outputs[0].path_from(self.task.generator.bld.bldnode)
		input_files = []

		cmd = " ".join(self.task.last_cmd)

		for node in self.task.inputs:
			cmd = cmd.replace(node.path_from(self.task.get_cwd()), node.abspath())
			input_files.append(node.abspath())

		cmd = cmd.replace(self.task.outputs[0].abspath(), output_file)

		for lib in self.task.env.STLIBPATH:
			cwd = self.task.get_cwd().abspath()
			path = os.path.normpath(os.path.join(cwd, lib))
			cmd = cmd.replace(self.task.env.STLIBPATH_ST % lib,
							  self.task.env.STLIBPATH_ST % path)

		for lib in self.task.env.LIBPATH:
			cwd = self.task.get_cwd().abspath()
			path = os.path.normpath(os.path.join(cwd, lib))
			cmd = cmd.replace(self.task.env.LIBPATH_ST % lib,
							  self.task.env.LIBPATH_ST % path)

		writer.build(
			rule=self.task_type,
			outputs=output_file,
			inputs=input_files,
			variables={
				"cmd": cmd,
			})
		writer.newline()


class CxxStLibAdapter(CStLibAdapter):
	task_type = "cxxstlib"  # type: str


class CxxShLibAdapter(CShLibAdapter):
	task_type = "cxxshlib"  # type: str


def get_subclasses(cls):
	subs = set()
	for sub in cls.__subclasses__():
		subs.add(sub)
		subs.update(get_subclasses(sub))
	return list(subs)


Adapters = get_subclasses(TaskAdapter)  # type: List[Type[TaskAdapter]]


class NinjaContext(Build.BuildContext):
	cmd = "ninja"

	def execute(self):
		self.restore()

		tasks = []  # type: List[TaskAdapter]

		if not self.all_envs:
			self.load_envs()

		self.recurse([self.run_dir])
		self.pre_build()

		def exec_command(self, *k, **kw):
			return 0

		for group in self.groups:
			for task_gen in group:
				try:
					if hasattr(task_gen, "post"):
						task_gen.post()
				except AttributeError:
					pass

				if isinstance(task_gen, Task.Task):
					current_tasks = [task_gen]
				else:
					current_tasks = task_gen.tasks

				for task in current_tasks:
					try:
						adapter = next(a for a in Adapters if a.task_type == task.__class__.__name__)
					except StopIteration:
						continue

					if adapter:
						try:
							tasks.append(adapter(task))
						except ValueError:
							continue

						task.nocache = True

						old_exec = task.exec_command
						task.exec_command = exec_command
						try:
							task.run()
						except Exception as e:
							Logs.error("Error running task {}: {}".format(task, e))
						finally:
							task.exec_command = old_exec

		ninja_file_node = self.bldnode.make_node("build.ninja")

		Logs.info("Ninja build commands will be stored in %s", ninja_file_node.abspath())

		string_buffer = StringIO()
		writer = Writer(string_buffer)

		writer.variable(key="ninja_required_version", value="1.5")
		writer.newline()

		for a in Adapters:
			a.write_rule(writer)

		writer.rule(
			"waf_build",
			command="python {} build {} {} {} --targets=$tgt".format(
				os.path.join(self.top_dir, "scripts", "build-ninja.py"),
				self.top_dir, os.path.dirname(self.out_dir), Options.lockfile)
		)
		writer.newline()

		writer.rule(
			"waf_build_all",
			command="python {} build {} {} {}".format(os.path.join(self.top_dir, "scripts", "build-ninja.py"),
													  self.top_dir, os.path.dirname(self.out_dir), Options.lockfile)
		)
		writer.newline()

		writer.rule(
			"waf_clean",
			command="python {} clean {} {} {}".format(os.path.join(self.top_dir, "scripts", "build-ninja.py"),
													  self.top_dir, os.path.dirname(self.out_dir), Options.lockfile)
		)
		writer.newline()

		for task in tasks:
			task.write_build(writer)

		for task in tasks:
			task.write_target(writer)

		outputs = []  # type: List[str]
		for task in tasks:
			outputs += task.get_outputs()

		writer.build(
			outputs="all",
			rule="phony",
			inputs=outputs
		)
		writer.newline()

		inputs = []  # type: List[str]
		for task in tasks:
			inputs += task.get_inputs()

		writer.build(
			outputs="all.passthrough",
			rule="waf_build_all",
			inputs=inputs
		)
		writer.newline()

		writer.build(outputs="clean", rule="waf_clean")
		writer.newline()

		writer.default("all")

		file_content = string_buffer.getvalue()

		with io.open(ninja_file_node.abspath(), "w", encoding="utf-8") as f:
			f.write(file_content)
