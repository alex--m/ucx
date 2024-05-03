#!/usr/bin/python

from os.path import join
from osu import Osu, OsuResults
from common import common_main

TIMEOUT = 600 # in seconds (set to 10 minutes)

UCX_COLLECTIVE_SM_TL_LIST = [#"locked",
                             "atomic",
                             "hypothetic",
                             #"counted_slots",
                             #"flagged_slots",
                             "collaborative"]

MPI_COLLECTIVE_COMPONENTS = ["acoll",     # Targeting AMD EPYC CPUs
                             "adapt",
                             "basic",     # Naive implementation, for baseline
                             "hcoll",     # Mellanox's Proprietary library (now UCC)
                             #"portals4", # Sandia (requires special HW!)
                             "smdirect",  # Shared-memory (only)
                             "tuned",     # by UTK (TN, USA)
                             #"ucx",      # based on (x)UCG
                             "han",       # Aggregates other components
                             #"inter",    # Inter-host (only)
                             "libnbc",	  # From SPCL group, ETH (Zurich)
                             #"sync",     # Only adds barrier to other components
                             "ucc",       # by UCF Collectives working-group
                             "xhc",       # From XHC-OpenMPI by ICS Forth
                             "yhccl"]

OSU_COLLECTIVE_NAMES = ["allgather", "allgatherv", "allreduce", "alltoall",
                        "alltoallv", "barrier", "bcast", "gather", "gatherv",
                        "iallgather", "iallgatherv", "iallreduce", "ialltoall",
                        "ialltoallv", "ialltoallw", "ibarrier", "ibcast",
                        "igather", "igatherv", "ireduce", "iscatter",
                        "iscatterv", "reduce", "reduce_scatter", "scatter",
                        "scatterv"]

OSU_COLLECTIVE_TEST = "mpi/collective/osu_%s"

def coll_test_osu(results_path, coll_list, ppn=2, osu_path=None, mpirun_path=None,
                  quick_check=True, env=[], is_slurm=False, is_host_only=False,
                  is_net_only=False, avg_expected=None, normal_variance=None,
                  check_threshold=False):
	o = Osu(results_path, osu_path, mpirun_path)
	if quick_check:
		for test in ["bcast", "reduce", "allreduce"]:
			o.test([OSU_COLLECTIVE_TEST % test], ppn=ppn,
			       extra_env=env, mca_pml=["ucx"],
			       mca_coll=coll_list, timeout=TIMEOUT,
			       is_host_only=is_host_only,
			       is_net_only=is_net_only,
			       avg_expected=avg_expected,
			       normal_variance=normal_variance)
	else:
		o.test(map(lambda x: OSU_COLLECTIVE_TEST % x,
		           OSU_COLLECTIVE_NAMES),
		       extra_env=env, mca_pml=["ucx"],
		       mca_coll=coll_list, timeout=TIMEOUT)

	return OsuResults(join(results_path, "osu")).parse()

def coll_test_osu_sm_tl(results_path, coll_list, ppn, osu_path, mpirun_path,
                        quick_check, ucx_tl, coll_tl, suffix, avg_expected,
                        normal_variance):
	tls = [ucx_tl + "_p2p",
	       "{}_{}_{}".format(ucx_tl, coll_tl, "bcast"),
	       "{}_{}_{}".format(ucx_tl, coll_tl, "incast")]
	return coll_test_osu(results_path + "_just_" + suffix, coll_list,
	                     ppn, osu_path, mpirun_path, quick_check,
	                     ["UCX_TLS=self," + ",".join(tls),
	                      "UCX_UCG_OVER_UCT_MULTIROOT_THRESH=256",
	                      "UCX_UCG_OVER_UCT_INTRANODE_TREE_THRESH=256"],
	                     normal_variance=normal_variance,
	                     avg_expected=avg_expected)

def coll_test_ucx_transport(results_path, ppn=2, osu_path=None, mpirun_path=None,
                            host_list="localhost", ucx_tl=None, avg_lat=None,
                            quick_check=True):
	res = {}
	coll_list = MPI_COLLECTIVE_COMPONENTS

	if ucx_tl in ["posix", "sysv", "xpmem"]:
		res["default"] = coll_test_osu(results_path, coll_list, ppn,
		                               osu_path, mpirun_path,
		                               quick_check, is_host_only=True)

		if avg_lat is not None:
			# Uniform distribution
			for unif_exp in (avg_lat, 10*avg_lat, 100*avg_lat):
				id = "default_uniform_%.7f" % unif_exp
				res[id] = coll_test_osu(results_path, coll_list,
				                        ppn, osu_path, mpirun_path,
				                        quick_check,
				                        avg_expected=unif_exp,
				                        is_host_only=True)

			# Normal distribution
			for norm_pair in [(avg_lat,     avg_lat),
			                  (avg_lat*10,  avg_lat),
			                  (avg_lat*100, avg_lat*10)]:
				id = "default_normal_%.7f" % norm_pair[0]
				res[id] = coll_test_osu(results_path, coll_list,
				                        ppn, osu_path, mpirun_path,
				                        quick_check,
				                        normal_variance=norm_pair[1],
				                        avg_expected=norm_pair[0],
				                        is_host_only=True)

		coll_list = ["ucx"]
		for coll_tl in UCX_COLLECTIVE_SM_TL_LIST:
			res[coll_tl] = coll_test_osu_sm_tl(results_path, coll_list,
			                                   ppn, osu_path,
			                                   mpirun_path,
			                                   quick_check, ucx_tl,
			                                   coll_tl, coll_tl,
			                                   None, None)

			res[coll_tl] = coll_test_osu_sm_tl(results_path, coll_list,
			                                   int(ppn/2), osu_path,
			                                   mpirun_path,
			                                   quick_check, ucx_tl,
			                                   coll_tl, coll_tl,
			                                   None, None)

			for procs in (2**p for p in range(1, int(ppn/2)) if 2**p < int(ppn/2)):
				res[coll_tl] = coll_test_osu_sm_tl(results_path,
				                                   coll_list,
				                                   procs, osu_path,
				                                   mpirun_path,
				                                   quick_check, ucx_tl,
				                                   coll_tl, coll_tl,
				                                   None, None)

			if avg_lat is not None:
				# Uniform distribution
				for unif_exp in (avg_lat, 10*avg_lat, 100*avg_lat):
					id = "%s_uniform_%i" % (coll_tl, unif_exp)
					res[id] = coll_test_osu_sm_tl(results_path,
					                              coll_list,
					                              ppn, osu_path,
					                              mpirun_path,
					                              quick_check,
					                              ucx_tl, coll_tl,
					                              id, unif_exp,
					                              None)

				# Normal distribution
				for norm_pair in [(avg_lat,     avg_lat),
				                  (avg_lat*10,  avg_lat),
				                  (avg_lat*100, avg_lat*10)]:
					id = "%s_normal_%i" % (coll_tl, norm_pair[0])
					res[id] = coll_test_osu_sm_tl(results_path,
					                              coll_list,
					                              ppn, osu_path,
					                              mpirun_path,
					                              quick_check,
					                              ucx_tl, coll_tl,
					                              id, *norm_pair)
	else:
		coll_list.append("ucx")
		res["default"] = coll_test_osu(results_path, coll_list, ppn,
		                               osu_path, mpirun_path, quick_check)

	return res

if __name__ == "__main__":
	print(coll_test_ucx_transport(*common_main("coll.py")))
