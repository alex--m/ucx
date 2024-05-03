#!/usr/bin/python

from os.path import join
from common import common_main
from perftest import Perftest
from osu import Osu, OsuResults

OSU_COLL_TEST         = "mpi/collective/osu_allreduce"
OSU_P2P_TEST_TEMPLATE = "mpi/pt2pt/osu_%s"
OSU_P2P_TEST_NAMES    = ["bw",         # Bandwidth
                         "bibw",       # Bidirectional bandwidth
                         "latency",    # Latency
                         "latency_mp", # Latency (multi-process)
                         "latency_mt", # Latency (multi-thread)
                         "mbw_mr",
                         "multi_lat"]

def p2p_test_perftest(results_path, osu_path=None, mpirun_path=None, ucx_tl=None,
                      quick_check=True):
	p = Perftest(results_path)

	if quick_check:
		p.test(["ib_send"])
	else:
		p.test()

	return { "perftest" : p.parse() }

def p2p_test_osu(results_path, osu_path=None, mpirun_path=None,
                 quick_check=True, env=[]):
	o = Osu(results_path, osu_path, mpirun_path)

	if quick_check:
		o.test([OSU_P2P_TEST_TEMPLATE % "latency", OSU_COLL_TEST])
	else:
		o.test(map(lambda x: OSU_P2P_TEST_TEMPLATE % x, OSU_P2P_TEST_NAMES) +
		       [OSU_COLL_TEST])

	# TODO: create an artifact by calling something like -
	# OsuResults(join(results_path, "osu")).export_csv(self, csv_file_name="osu_results.csv"):

	return { "osu" : OsuResults(join(results_path, "osu")).parse() }

def p2p_test_ucx_transport(results_path, ppn=2, osu_path=None, mpirun_path=None,
                           host_list="localhost", ucx_tl=None, avg_lat=None,
                           quick_check=True):
	res = {}
	res.update(p2p_test_perftest(results_path, quick_check))
	if ucx_tl is not None:
		res.update(p2p_test_osu(results_path + "_no_" + ucx_tl, osu_path,
		                        mpirun_path, quick_check,
		                        ["UCX_TLS=^" + ucx_tl])) # exclude the transport

	res.update(p2p_test_osu(results_path, osu_path, mpirun_path, quick_check))

	return res

if __name__ == "__main__":
	print(p2p_test_ucx_transport(*common_main("p2p.py")))
