//
//  jet/TaskListView.qml
//
//  Created by Sam Gateau, 2018/05/09
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.7
import QtQuick.Controls 1.4 as Original
import QtQuick.Controls.Styles 1.4

import "qrc:///qml/styles-uit"
import "qrc:///qml/controls-uit" as HifiControls

import "../jet.js" as Jet

Rectangle {
    HifiConstants { id: hifi;}
    color: hifi.colors.baseGray;
    id: root

 //   width: parent ? parent.width : 200
 //   height: parent ? parent.height : 400
    property var rootConfig : Workload
    property var myArray : []

    Component.onCompleted: {
        var message = ""
       // functor = Jet.job_print_functor(function (line) { message += line + "\n"; }, false);
        
      //  functor = Jet.job_list_functor(root.myArray);

        var maxDepth = 4;
        
        var jobTreePath = []
        var jobsRoot;

        var functor = function (job, depth, index) {
            var newItem = {"name": job.objectName, "level": depth, "index": index, "subNode": [], "init": depth < maxDepth, "path": ""}
            if (depth == 0) {
                jobsModel.append(newItem)
                jobsRoot = jobsModel.get(0).subNode;
            } else {
                if (jobTreePath.length < depth) {
                    var node = jobsRoot;
                    var path;
                    for (var n = 0; n < jobTreePath.length; n++) {
                        newItem.path += (n > 0 ? "." : "") + node.get(jobTreePath[n]).name
                        node = node.get(jobTreePath[n]).subNode
                    }
                    node.append(newItem)
                    jobTreePath.push(0);
                } else if (jobTreePath.length >= depth) {
                    var node = jobsRoot;
                    for (var n = 0; n < (depth - 1); n++) {
                        newItem.path += (n > 0 ? "." : "") + node.get(jobTreePath[n]).name
                        node = node.get(jobTreePath[n]).subNode
                    }
                    node.append(newItem)
                    jobTreePath[depth-1] = index;
                    while (jobTreePath.length > depth) {
                        jobTreePath.pop();
                    }                       
                }
            }
            return true;
        }

        Jet.task_traverseTree(rootConfig, functor);
    }

    ListModel {
        id: jobsModel
    }

    Component {
        id: objRecursiveDelegate
        Column {
            id: objRecursiveColumn
            clip: true
            visible: model.init
            
            MouseArea {
                width: objRow.implicitWidth
                height: objRow.implicitHeight 
                onDoubleClicked: {
                    for(var i = 1; i < parent.children.length - 1; ++i) {
                        parent.children[i].visible = !parent.children[i].visible
                    }
                }
                Row {
                    id: objRow
                    Item {
                        height: 1
                        width: model.level * 15
                    }
                    HifiControls.CheckBox {
                        property var config: root.rootConfig.getConfig(model.path + "." + model.name);
                        text: (objRecursiveColumn.children.length > 2 ?
                                objRecursiveColumn.children[1].visible ?
                                qsTr("-  ") : qsTr("+ ") : qsTr("   ")) + model.name + " ms=" + config.cpuRunTime.toFixed(3)
                        checked: config.enabled
                    }
                }
            }
            Repeater {
                model: subNode
                delegate: objRecursiveDelegate
            }
        }
    }

    ListView {
        id: theView
        anchors.fill: parent
        model: jobsModel
        delegate: objRecursiveDelegate
    }

}