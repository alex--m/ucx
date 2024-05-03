#!/usr/bin/python

# TODO: Test only PPN=1 and PPN=FULL (no need to pass anything! :)

from os import getcwd
from csv import writer
from time import sleep
from subprocess import call
from os.path import join, split, exists
from common import RESULTS_FOLDER, gen_mpirun_cmd

import os, sys, subprocess, csv, fcntl

UCX_NET_ONLY    = ["UCX_TLS=self,cm,rc"]
UCX_HOST_ONLY   = ["UCX_TLS=^tcp,ib", "UCX_UCG_OVER_UCT_MULTIROOT_THRESH=256"]
UCX_THRESHOLDS  = {
	"short-only" : ["UCX_BUILTIN_SHORT_MAX_TX_SIZE=inf",
					"UCX_BUILTIN_BCOPY_MAX_TX_SIZE=inf",
					"UCX_BUILTIN_MEM_REG_OPT_CNT=1000000"],
	"bcopy-only" : ["UCX_BUILTIN_SHORT_MAX_TX_SIZE=0",
					"UCX_BUILTIN_BCOPY_MAX_TX_SIZE=inf",
					"UCX_BUILTIN_MEM_REG_OPT_CNT=1000000"],
	"zcopy-only" : ["UCX_BUILTIN_SHORT_MAX_TX_SIZE=0",
					"UCX_BUILTIN_BCOPY_MAX_TX_SIZE=0"]
	}

class OsuParser(object):
	def __init__(self, file_path, file_name):
		osu, test, host_cnt, ppn, pml, coll, is_coll_test, suffix = file_name.split("_")
		# e.g. osu_reduce_n-1_ppn-2_pml-ucx_coll-tuned_1_default.out
		assert(suffix.endswith(".out"))
		suffix = suffix[:-4]

		assert(osu == "osu")
		self._suffix       = suffix
		self._is_coll_test = bool(int(is_coll_test))
		self._pml          = pml[4:]
		self._coll         = coll[5:]
		self._test         = test
		self._host_cnt     = int(host_cnt[2:])
		self._ppn          = int(ppn[4:])
		self._path         = join(file_path, file_name)

	@staticmethod
	def filename_format(test, hosts, ppn, pml, coll, is_coll_test, suffix):
		assert("_" not in pml+coll+suffix)
		return "%s_n-%s_ppn-%s_pml-%s_coll-%s_%i_%s" % (test, hosts, ppn, pml,
		                                                coll, int(is_coll_test),
		                                                suffix)

	def readlines(self):
		with open(self._path) as result:
			for line in result.readlines():
				if not line[0].isdigit():
					continue
				try:
					sline = line.split()
					size  = int(sline[0])
					if self._is_coll_test:
						avg, min, max = map(float, sline[1:3])
						iters         = int(sline[4])
					else:
						avg = float(sline[1])
						min       = avg
						max       = avg
						iters     = 1
					yield (self._suffix, self._pml, self._coll, self._host_cnt,
						   self._ppn, self._test, size, avg, min, max, iters)
				except ValueError:
					continue

class OsuResults(object):
	CSV_HEADER = "Configuration|MCA PML|MCA COLL|Collective Type|Host Count|Ranks Per Host|Collectives Library|Threshold|Message Size|Avg Latency (us)|Min Latency (us)|Max Latency (us)|Iterations".split("|")

	def __init__(self, results_path):
		self._path = results_path

	def iter_file_paths(self):
		for file_path, dirs, files  in os.walk(self._path):
			for file_name in files:
				if file_name.endswith(".out"):
					yield file_path, file_name

	def parse(self):
		res = []
		for file_path, file_name in self.iter_file_paths():
			for entry in OsuParser(file_path, file_name).readlines():
				res.append(entry)
		return res

	def export_csv(self, csv_file_name="osu_results.csv"):
		with open(join(self._path, csv_file_name), "w") as csvfile:
			osu_writer = writer(csvfile)
			osu_writer.writerow(self.CSV_HEADER)
			for file_path in self.iter_file_paths():
				for entry in OsuParser(file_path).readlines():
					osu_writer.writerow(entry)

class Osu(object):
	URL         = "https://mvapich.cse.ohio-state.edu/download/mvapich/osu-micro-benchmarks-7.4.tar.gz"
	INSTALL_CMD = "tar -xzvf osu-micro-benchmarks-7.4.tar.gz && cd osu-micro-benchmarks-7.4 && CC=%s CXX=%s ./configure && make"
	EXIST_CHECK = "mpi/collective/osu_allreduce"
	COLL_PREFIX = "mpi/collective"
	FULL_RESULT = "-i 100000 -x 10000 -f -c 1"
	DEFAULT_END = "1048576" # last datapoint in all OSU tests by default

	def __init__(self, results_path, osu_path="/usr/libexec/osu-micro-benchmarks/",
	             mpirun_path="/usr/bin/mpirun"):
		if not exists(results_path):
			os.mkdir(results_path)

		self._out_path = join(results_path, "osu")
		if not exists(self._out_path):
			os.mkdir(self._out_path)

		self._mpirun_path = mpirun_path
		self._osu_path = osu_path

		# Make sure the path contains OMPI and OSU
		if not exists(mpirun_path):
			raise Exception("Couldn't find MPI at " + mpirun_path)

		if not exists(join(self._osu_path, self.EXIST_CHECK)):
			self.install()

	def install(self):
		self._osu_path = join(getcwd(), "osu-micro-benchmarks-7.3")
		if not exists(self._osu_path):
			print(self._osu_path + " not found - downloading...")
			call("wget " + self.URL, shell=True)
			call(self.INSTALL_CMD % (self._mpirun_path[:-3] + "cc",
									 self._mpirun_path[:-3] + "cxx"), shell=True)

	def _gen_single_osu_cmd(self, suffix, osu_test, node_cnt, ppn,
	                        partial_list, mca_pml, mca_coll, env_list,
	                        is_slurm, is_host_only, is_net_only,
	                        avg_expected, normal_variance):
		mca_list = []
		if mca_pml:
			mca_list.append(("pml", mca_pml))
			if mca_pml == "^ucx":
				mca_list.append(("btl", "^uct"))
		if mca_coll:
			mca_list.append(("coll", "self,basic,libnbc," + mca_coll))
			mca_list.append(("coll_%s_priority" % (mca_coll), "100"))
		if is_net_only:
			env_list.append(UCX_NET_ONLY)
		else:
			if not [x for x in env_list if "UCX_TLS" in x]:
				assert((not is_host_only) or (not is_net_only))
				if is_host_only:
					env_list.extend(UCX_HOST_ONLY)
				if is_net_only:
					env_list.extend(UCX_NET_ONLY)

		is_coll = osu_test.startswith(self.COLL_PREFIX)
		if is_coll:
			extra_arg = "-m 4:8192 " + self.FULL_RESULT
			self.DEFAULT_END = "8192   "
		else:
			extra_arg = ""

		if avg_expected:
			if normal_variance:
				extra_arg = extra_arg + " -I G:%i:%i" % (avg_expected, normal_variance)
			else:
				extra_arg = extra_arg + " -I U:%i" % avg_expected

		exe = split(osu_test)[1]
		cmd = gen_mpirun_cmd(mpirun_exec=self._mpirun_path,
		                     test=join(self._osu_path, osu_test),
		                     node_cnt=node_cnt,
		                     ppn=ppn,
		                     hostlist=partial_list,
		                     env_list=env_list,
		                     mca_list=mca_list,
		                     args=extra_arg)
		out = OsuParser.filename_format(test=osu_test.strip(), hosts=node_cnt,
		                                ppn=ppn, pml=mca_pml, coll=mca_coll,
		                                is_coll_test=is_coll, suffix=suffix)

		out = join(self._out_path, out)
		inner_path, _ = split(out)
		outer_path, _ = split(inner_path)
		if not exists(outer_path):
			os.mkdir(outer_path)
		if not exists(inner_path):
			os.mkdir(inner_path)

		return cmd, exe, out

	def _gen_osu_cmds(self, test_list, host_list, ppn, extra_env, mca_pml,
	                  mca_coll, is_slurm=False, is_host_only=False,
	                  is_net_only=False, avg_expected=None,
	                  normal_variance=None, check_threshold=False):
		cmd_list = []
		node_cnt = host_list.count(",") + 1
		seperator = ":%i," % ppn
		partial_list = seperator.join(host_list.split(",")[:node_cnt]) + seperator[:-1]

		# TODO: remove this:
		base_env = ["UCX_RNDV_THRESH=inf", "UCX_ADDRESS_VERSION=v2"]

		for osu_test in test_list:
			if not exists(join(self._osu_path, osu_test)):
				raise Exception("Couldn't find OSU test at " + osu_test)

			cmd_list.append(self._gen_single_osu_cmd("default",
			                                         osu_test,
			                                         node_cnt,
			                                         ppn,
			                                         partial_list,
			                                         mca_pml,
			                                         mca_coll,
			                                         base_env +
			                                         extra_env,
			                                         is_slurm,
			                                         is_host_only,
			                                         is_net_only,
			                                         avg_expected,
			                                         normal_variance))

			if mca_coll == "ucx" and check_threshold:
				for env_name, env_cmd in UCX_THRESHOLDS.items():
					cmd_list.append(self._gen_single_osu_cmd(env_name,
					                                         osu_test,
					                                         node_cnt,
					                                         ppn,
					                                         partial_list,
					                                         mca_pml,
					                                         mca_coll,
					                                         env_cmd +
					                                         extra_env,
					                                         is_slurm,
					                                         is_host_only,
					                                         is_net_only,
					                                         avg_expected,
					                                         normal_variance))
		return cmd_list

	def _check_out_file(self, out_file):
		return exists(out_file) and self.DEFAULT_END in open(out_file).read()

	def _execute(self, command, executable, output_path, timeout_seconds):
		out_file = output_path + ".out"
		err_file = output_path + ".err"
		if self._check_out_file(out_file):
			if exists(err_file):
				os.remove(err_file)
			print("Skipping.")
			return
		else:
			print("Executing: " + command)

		with open(out_file, 'w') as out:
			fcntl.flock(out, fcntl.LOCK_EX)
			out.write(command)
			out.write("\n\n")
			out.flush()
			with open(err_file, 'w') as err:
				if timeout_seconds:
					subprocess.call(command, stdout=out, stderr=err, shell=True,
					                timeout=timeout_seconds)
				else:
					subprocess.call(command, stdout=out, stderr=err, shell=True)
			fcntl.flock(out, fcntl.LOCK_UN)

		if executable is not None:
			subprocess.call("/usr/bin/killall " + executable, shell=True)
			while not subprocess.call("/usr/bin/pgrep " + executable, shell=True):
				print("Waiting for %s to terminate..." % executable)
				sleep(1)

		if not exists(err_file):
			return

		if os.path.getsize(err_file) == 0 or self._check_out_file(out_file):
			os.remove(err_file)
		else:
			print(err_file)
			with open(err_file, 'r') as err:
				print(err.read())
			print("\n")

		print(out_file)
		with open(out_file, 'r') as out:
			print(out.read())
		print("\n\n")

	def test(self, test_list, host_list="localhost", ppn=2, timeout=None,
	         extra_env=[], mca_pml=["", "^ucx"], mca_coll=[""],
	         is_slurm=False, is_host_only=False, is_net_only=False,
	         avg_lat=None, avg_expected=None, normal_variance=None,
	         check_threshold=False):
		commands = []
		for pml in mca_pml:
			for coll in mca_coll:
				commands.extend(self._gen_osu_cmds(test_list,
				                host_list, ppn, extra_env, pml,
				                coll, is_slurm, is_host_only,
				                is_net_only, avg_expected,
				                normal_variance, check_threshold))

		for cmd, executable, out in commands:
			try:
				if host_list != "localhost":
					executable = None
				self._execute(cmd, executable, out, timeout)
			except KeyboardInterrupt:
				raise
			except:
				print("Unexpected error:", sys.exc_info()[0])

if __name__ == "__main__":
	if len(sys.argv) < 2 or len(sys.argv) > 7:
		print("USAGE (#1 for parsing, #2 for execution):")
		print("1. osu.py <path_to_results>")
		print("2. osu.py <osu_test*> <comma_seperated_host_list> [<ppn> [<osu_build_path> [<mpirun_path> [<timeout_in_seconds**>]]]]")
		print(" *  Example: \"mpi/collective/osu_allreduce\"")
		print(" ** supported in Python 3.3+")
		sys.exit(1)

	if len(sys.argv) == 2:
		OsuResults(sys.argv[1]).export_csv()
	else:
		osu_test    = sys.argv[1]
		host_list   = sys.argv[2]
		ppn         = int(sys.argv[3])
		osu_path    = "."
		mpirun_path = "/usr/bin/mpirun"
		timeout     = None

		if len(sys.argv) > 4:
			osu_path = sys.argv[4]
		if len(sys.argv) > 5:
			mpirun_path = sys.argv[5]
		if len(sys.argv) > 6:
			timeout = int(sys.argv[6])

		Osu(RESULTS_FOLDER, osu_path, mpirun_path).test([osu_test], host_list, ppn, timeout)
		print(OsuResults(join(RESULTS_FOLDER, "osu")).parse())
