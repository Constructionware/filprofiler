from os.path import join
from setuptools import setup, Extension
from distutils import sysconfig
import sys


if sys.platform == "darwin":
    # Want a dynamiclib so that it can inserted with DYLD_INSERT_LIBRARIES:
    config_vars = sysconfig.get_config_vars()
    config_vars["LDSHARED"] = config_vars["LDSHARED"].replace("-bundle", "-dynamiclib")


def read(path):
    with open(path) as f:
        return f.read()


setup(
    name="filprofiler",
    packages=["filprofiler"],
    entry_points={"console_scripts": ["fil-profile=filprofiler._script:stage_1"],},
    ext_modules=[
        Extension(
            name="filprofiler._filpreload",
            sources=[join("filprofiler", "_filpreload.c")],
            extra_objects=[join("target", "release", "libpymemprofile_api.a")],
            extra_compile_args=["-fno-omit-frame-pointer"],
            extra_link_args=["-export-dynamic"],
        )
    ],
    package_data={"filprofiler": ["licenses.txt"],},
    use_scm_version=True,
    setup_requires=["setuptools_scm"],
    extras_require={
        "dev": [
            "pytest",
            "pampy",
            "numpy",
            "scikit-image",
            "cython",
            "black",
            "towncrier==19.9.0rc1",
            "wheel",
            "auditwheel",
            "twine",
        ],
    },
    description="A memory profiler for data batch processing applications.",
    classifiers=[
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: OS Independent",
        "Programming Language :: Python",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: Implementation :: CPython",
    ],
    python_requires=">=3.6",
    license="Apache 2.0",
    url="https://pythonspeed.com/products/filmemoryprofiler/",
    maintainer="Itamar Turner-Trauring",
    maintainer_email="itamar@pythonspeed.com",
    long_description=read("README.md"),
    long_description_content_type="text/markdown",
)
