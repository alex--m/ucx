#!/usr/bin/python

import os, sys

RESULTS_FOLDER      = "results"
USAGE               = "USAGE:\n %s <path_to_results> [<ppn> [<path_to_osu_libexec> [<path_to_mpirun> [<coll_ucx_transport> [<imbalance>]]]]]"

MPIRUN_MCA_SLURM    = "OMPI_MCA_%s=%s"
MPIRUN_MCA_DEFAULT  = "--mca %s %s"

MPIRUN_CMD_SLURM    = "{mca_args} {env_list} {mpirun_exec} --nodes={node_cnt} --ntasks-per-node={ppn} {test} {args}"
MPIRUN_CMD_DEFAULT  = "{mpirun_exec} {mca_args} --allow-run-as-root --map-by core --bind-to core -H {hostlist} {env_list} {test} {args}"

def use_slurm():
	return "SLURM_JOB_ID" in os.environ and "TEST_USING_SLURM" in os.environ

def gen_mpirun_cmd(mca_list=[], **kwargs):
	if use_slurm():
		kwargs["mpirun_exec"] = "/usr/bin/srun"
		mca_format = MPIRUN_MCA_SLURM
		cmd_format = MPIRUN_CMD_SLURM
		kwargs["env_list"] = " ".join(kwargs.get("env_list", []))
	else:
		mca_format = MPIRUN_MCA_DEFAULT
		cmd_format = MPIRUN_CMD_DEFAULT
		kwargs["env_list"] = "-x " + " -x ".join(kwargs.get("env_list", []))

	kwargs["mca_args"] = " ".join([mca_format % (x, y) for x, y in mca_list])
	return cmd_format.format(**kwargs)

def common_main(script_name):
	if len(sys.argv) < 2 or len(sys.argv) > 8:
		print(USAGE % script_name)
		sys.exit(1)

	# Create results folder
	folder = os.path.join(sys.argv[1], RESULTS_FOLDER)
	if not os.path.exists(folder):
		os.mkdir(folder)

	# Detect the number of processes per node (PPN)
	ppn = 2
	if len(sys.argv) > 2:
		ppn = int(sys.argv[2])

	# Detect OSU path
	osu_path = None
	if len(sys.argv) > 3:
		osu_path = sys.argv[3]

	# Detect path to `mpirun` executable
	mpirun_path = None
	if len(sys.argv) > 4:
		mpirun_path = sys.argv[4]

	# Detect the list of hosts to run on
	host_list = None
	if len(sys.argv) > 5:
		host_list = sys.argv[5]

	# Choose a "base-transport" for collective tests (currently: sysv/posix)
	ucx_tl = None
	quick_check = True
	if len(sys.argv) > 6:
		ucx_tl = sys.argv[6]

	# Measure the effect of imbalance - given the average latency in nanoseconds
	avg_lat = None
	if len(sys.argv) > 7:
		avg_lat = int(sys.argv[7])

	return (folder, ppn, osu_path, mpirun_path, host_list, ucx_tl, avg_lat, quick_check)
