#!/usr/bin/python

# TODO: Test only PPN=1 and PPN=FULL (no need to pass anything! :)

from fcntl import *
from os import getcwd, mkdir
from os.path import join, exists
from subprocess import call, run
from common import RESULTS_FOLDER

class Perftest(object):
	GIT_REPO    = "https://github.com/linux-rdma/perftest.git"
	INSTALL_CMD = "cd perftest && ./autogen.sh && ./configure && make"
	TYPES       = ["ib_send", "ib_read", "ib_write", "ib_atomic", "raw_ethernet_send"]
	METRICS     = ["bw", "lat"]

	def __init__(self, results_path, perftest_path="/usr/bin"):
		self._out_path = join(results_path, "perftest")
		if not exists(self._out_path):
			mkdir(self._out_path)
		self._bin_path = perftest_path
		if not exists(join(self._bin_path, "ib_send_bw")):
			self.install()

	def install(self):
		self._bin_path = join(getcwd(), "perftest")
		if not exists(self._bin_path):
			call("git clone " + self.GIT_REPO, shell=True)
			call(self.INSTALL_CMD, shell=True)

	def _parse_one(self, result_file):
		return {} # TODO

	def _gen_file_name(self, test_type, metric):
		return "%s_%s" % (test_type, metric)

	def _gen_output_file_name(self, test_type, metric):
		return join(self._out_path,
					"%s.out" % self._gen_file_name(test_type, metric))

	def _gen_error_file_name(self, test_type, metric):
		return join(self._out_path,
					"%s.err" % self._gen_file_name(test_type, metric))

	def parse(self):
		res = {}
		for test_type in self.TYPES:
			for metric in self.METRICS:
				result_file = self._gen_file_name(test_type, metric)
				if exists(result_file):
					if test_type not in res:
						res[test_type] = {}
					res[test_type][metric] = self._parse_one(result_file)
		return res

	def _execute(self, file_name, extra_args, host, out_path=None, err_path=None):
		assert(host is None) # Otherwise SSH?
		perftest_cmd = join(self._bin_path, file_name) + extra_args
		if out_path:
			if not err_path:
				err_path = out_path
			with open(out_path, 'w') as out:
				flock(out, LOCK_EX | LOCK_NB)
				out.write(perftest_cmd)
				out.write("\n\n")
				out.flush()
				with open(err_path, 'w') as err:
					run(perftest_cmd, stdout=out, stderr=err, shell=True)
		else:
			return call(perftest_cmd, shell=True)

	def _run_server(self, executable_name, server_host, out_path, err_path):
		self._execute(executable_name, "", server_host, out_path, err_path)

	def _run_client(self, executable_name, client_host):
		if client_host:
			args = " %s" % client_host
		else:
			args = ""
		return self._execute(executable_name, args, client_host)

	def test(self, types=None, metrics=None, server_host=None, client_host=None):
		if types is None:
			types = self.TYPES
		if metrics is None:
			metrics = self.METRICS

		for test_type in types:
			for metric in metrics:
				executable_name  = self._gen_file_name(test_type, metric)
				out_path = self._gen_output_file_name(test_type, metric)
				err_path = self._gen_error_file_name(test_type, metric)
				self._run_server(executable_name, server_host, out_path, err_path)
				print(self._run_client(executable_name, client_host))

if __name__ == "__main__":
	print(Perftest(RESULTS_FOLDER).test())
