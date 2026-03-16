import os

import vsauto
from pathlib import Path

# Use os.path.join for cross-platform compatibility
namespace = "vscarefulsource"


@vsauto.hookimpl
def loadPlugin():
    import importlib.util
    import logging
    from pathlib import Path

    import vapoursynth as vs

    # Gracefully handling cases where vapoursynth has autoloaded a plugin
    # with the same namespace is strongly encouraged.
    preloaded = next(
        (plugin for plugin in vs.core.plugins() if plugin.namespace == namespace), None
    )
    if preloaded:
        logging.warning(
            f"A plugin at '{Path(preloaded.plugin_path)}' has prevented loading '{__name__}'. Please remove the conflicting package or auto-loaded plugin if you wish to use this version."
        )
        return

    origin = importlib.util.find_spec("vscarefulsource_ext").origin

    if os.name == "nt":
        new_path = Path(origin).parent / ".vscarefulsource.mesonpy.libs"
        os.add_dll_directory(new_path)
    vs.core.std.LoadPlugin(origin)
