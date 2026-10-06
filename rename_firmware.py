Import("env")
import os
import re
import shutil

def after_build(source, target, env):
    firmware_path = target[0].get_abspath()
    build_dir = os.path.dirname(firmware_path)

    header_path = os.path.join(env.get("PROJECT_DIR"), "include", "thingsboard_client.h")
    hw_id = None

    try:
        with open(header_path, "r", encoding="utf-8") as f:
            content = f.read()
            match = re.search(r'#define\s+TB_HW_ID\s+"([^"]+)"', content)
            if match:
                hw_id = match.group(1)
    except Exception as e:
        print(f"Warning: Could read {header_path}: {e}")

    if hw_id:
        new_path = os.path.join(build_dir, f"{hw_id}.bin")
        shutil.copyfile(firmware_path, new_path)
        print(f"\n=======================================================")
        print(f"Firmware generated: {new_path}")
        print(f"=======================================================\n")

# Hook on firmware.bin generation
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)
