import uproot
import numpy as np
import matplotlib.pyplot as plt
import mplhep as hep
plt.style.use(hep.style.CMS)

f_custom = uproot.open("/vols/cms/saa220/scratch1/CMSSW_16_0_3/src/out.root")
# f = uproot.open("/vols/cms/saa220/scratch1/CMSSW_16_0_3/src/ScoutingNano.root")
events_custom = f_custom.get("Events")
print(f_custom.keys())
# events = f.get("Events")
Feature = "SuepEvent_Ht"
PFFeature = events_custom[Feature].array(library="np")
# PFFeature = PFFeature[PFFeature > 0]
print(PFFeature.shape)
if len(PFFeature.shape) > 1 : PFFeature = np.concatenate(PFFeature)
plt.hist((PFFeature), bins=20, density=False, histtype='step', color='b', label="2024D PFMonitor")
plt.xlabel(f"{Feature}")
plt.ylabel("Events")
plt.yscale("log")
plt.legend()
hep.cms.label("Private Work", data=True, com=13.6, lumi=None, loc=0)
plt.savefig(f"{Feature}.png", bbox_inches="tight")

