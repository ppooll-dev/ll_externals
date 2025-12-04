{
    "patcher": {
        "fileversion": 1,
        "appversion": {
            "major": 9,
            "minor": 1,
            "revision": 1,
            "architecture": "x64",
            "modernui": 1
        },
        "classnamespace": "box",
        "rect": [ 448.0, 178.0, 676.0, 711.0 ],
        "subpatcher_template": "Untitled3_template",
        "boxes": [
            {
                "box": {
                    "id": "obj-26",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 444.0, 35.0, 153.0, 20.0 ],
                    "text": "by klaus filip & joe steccato"
                }
            },
            {
                "box": {
                    "fontsize": 18.0,
                    "id": "obj-24",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 124.0, 28.0, 218.0, 27.0 ],
                    "text": "ll_externals"
                }
            },
            {
                "box": {
                    "id": "obj-22",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 229.0, 582.0, 36.0, 22.0 ],
                    "text": "ll_zip"
                }
            },
            {
                "box": {
                    "id": "obj-21",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 582.0, 85.0, 22.0 ],
                    "text": "ll_zip.maxhelp"
                }
            },
            {
                "box": {
                    "id": "obj-20",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 181.0, 541.0, 84.0, 22.0 ],
                    "saved_object_attributes": {
                        "enabled": 1
                    },
                    "text": "ll_filewatchers"
                }
            },
            {
                "box": {
                    "id": "obj-18",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 541.0, 133.0, 22.0 ],
                    "presentation_linecount": 2,
                    "text": "ll_filewatchers.maxhelp"
                }
            },
            {
                "box": {
                    "fontface": 0,
                    "fontname": "Verdana",
                    "fontsize": 10.0,
                    "id": "obj-17",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 186.0, 508.0, 79.0, 21.0 ],
                    "text": "ll_fastforward"
                }
            },
            {
                "box": {
                    "id": "obj-16",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 508.0, 129.0, 22.0 ],
                    "presentation_linecount": 2,
                    "text": "ll_fastforward.maxhelp"
                }
            },
            {
                "box": {
                    "bgcolor": [ 0.0, 0.706048488616943, 0.935728669166565, 1.0 ],
                    "id": "obj-15",
                    "maxclass": "ll_slishi",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 191.0, 406.0, 74.0, 73.0 ]
                }
            },
            {
                "box": {
                    "id": "obj-13",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 415.0, 96.0, 22.0 ],
                    "text": "ll_slishi.maxhelp"
                }
            },
            {
                "box": {
                    "hidden": 1,
                    "id": "obj-11",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 480.0, 232.0, 45.0, 22.0 ],
                    "text": "spread"
                }
            },
            {
                "box": {
                    "altcolor": [ 0.784314, 0.090097, 0.0, 1.0 ],
                    "amount": 3,
                    "bgcolor": [ 0.243137254901961, 0.043137254901961, 0.482352941176471, 1.0 ],
                    "fontface": 0,
                    "fontsize": 13.0,
                    "id": "obj-9",
                    "maxclass": "ll_2dslider",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 124.0, 306.0, 141.0, 74.0 ],
                    "pointsize": 16,
                    "slidercolor": [ 0.529369, 0.529369, 0.529369, 1.0 ],
                    "varname": "2d",
                    "xgrid_pattern": [ 0.0 ],
                    "ygrid_pattern": [ 0.0 ]
                }
            },
            {
                "box": {
                    "id": "obj-8",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 331.0, 111.0, 22.0 ],
                    "text": "ll_2dslider.maxhelp"
                }
            },
            {
                "box": {
                    "hidden": 1,
                    "id": "obj-1",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "bang" ],
                    "patching_rect": [ 444.0, 153.0, 62.0, 22.0 ],
                    "text": "loadbang"
                }
            },
            {
                "box": {
                    "hidden": 1,
                    "id": "obj-19",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 444.0, 182.0, 43.0, 22.0 ],
                    "text": "set so"
                }
            },
            {
                "box": {
                    "hidden": 1,
                    "id": "obj-12",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "float", "bang" ],
                    "patching_rect": [ 444.0, 126.0, 135.0, 22.0 ],
                    "text": "buffer~ so drumLoop.aif"
                }
            },
            {
                "box": {
                    "bgcolor": [ 0.847058823529412, 0.847058823529412, 0.847058823529412, 1.0 ],
                    "chans": 0,
                    "id": "obj-7",
                    "maxclass": "ll_mcwaveform",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 124.0, 205.0, 141.0, 76.0 ],
                    "setmode": 3,
                    "varname": "mcwaveform",
                    "wfcolor": [ 0.495044440031052, 0.109287068247795, 0.842489719390869, 1.0 ]
                }
            },
            {
                "box": {
                    "id": "obj-6",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 226.0, 137.0, 22.0 ],
                    "presentation_linecount": 2,
                    "text": "ll_mcwaveform.maxhelp"
                }
            },
            {
                "box": {
                    "bgcolor": [ 0.24313725490196078, 0.09411764705882353, 0.3137254901960784, 1.0 ],
                    "fontface": 0,
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "id": "obj-3",
                    "items": [ "size", ",", "and", ",", "fontsize", ",", "are", ",", "independent", ",", "-", ",", "show", ",", "by", ",", "message" ],
                    "maxclass": "ll_menu",
                    "menufontsize": 12.0,
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "", "", "" ],
                    "outputcancel": 1,
                    "parameter_enable": 0,
                    "patching_rect": [ 124.0, 142.0, 141.0, 28.0 ],
                    "prefix": "",
                    "varname": "ll_menu"
                }
            },
            {
                "box": {
                    "id": "obj-5",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 145.0, 99.0, 22.0 ],
                    "text": "ll_menu.maxhelp"
                }
            },
            {
                "box": {
                    "annotation": "",
                    "bgcolor": [ 0.741176, 1.0, 0.831373, 1.0 ],
                    "fontface": 0,
                    "fontsize": 23.0,
                    "format": [ 1.13 ],
                    "hint": "",
                    "id": "obj-34",
                    "label": [ "wert" ],
                    "labelcolor": [ 0.933333, 0.066667, 0.066667, 1.0 ],
                    "maxclass": "ll_number",
                    "min": 1.0,
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 124.0, 91.0, 141.0, 26.0 ],
                    "selectcolor": [ 0.45098, 0.643137, 1.0, 0.47451 ],
                    "slidercolornofocus": [ 0.458824, 0.458824, 0.458824, 0.584314 ],
                    "slidermax": 100.0,
                    "sliderstyle": 0,
                    "textcolornofocus": [ 0.482353, 0.482353, 0.482353, 0.792157 ],
                    "varname": "slid",
                    "vertical": 28.0
                }
            },
            {
                "box": {
                    "id": "obj-4",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 93.0, 109.0, 22.0 ],
                    "text": "ll_number.maxhelp"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "hidden": 1,
                    "id": "obj-30",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 630.0, 81.0, 22.0 ],
                    "text": "prepend load"
                }
            },
            {
                "box": {
                    "fontname": "Arial",
                    "fontsize": 12.0,
                    "hidden": 1,
                    "id": "obj-14",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 290.0, 657.0, 53.0, 22.0 ],
                    "text": "pcontrol"
                }
            }
        ],
        "lines": [
            {
                "patchline": {
                    "destination": [ "obj-11", 0 ],
                    "hidden": 1,
                    "order": 0,
                    "source": [ "obj-1", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-19", 0 ],
                    "hidden": 1,
                    "order": 1,
                    "source": [ "obj-1", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-9", 0 ],
                    "hidden": 1,
                    "source": [ "obj-11", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "hidden": 1,
                    "source": [ "obj-13", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "hidden": 1,
                    "source": [ "obj-16", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "hidden": 1,
                    "source": [ "obj-18", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-7", 0 ],
                    "hidden": 1,
                    "source": [ "obj-19", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "hidden": 1,
                    "source": [ "obj-21", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-14", 0 ],
                    "hidden": 1,
                    "source": [ "obj-30", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "hidden": 1,
                    "source": [ "obj-4", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "hidden": 1,
                    "source": [ "obj-5", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "hidden": 1,
                    "source": [ "obj-6", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-30", 0 ],
                    "hidden": 1,
                    "source": [ "obj-8", 0 ]
                }
            }
        ],
        "autosave": 0
    }
}