//
//  ParticleTree.cpp
//  hifi
//
//  Created by Brad Hefta-Gaub on 12/4/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#include "ParticleTree.h"

ParticleTree::ParticleTree(bool shouldReaverage) : Octree(shouldReaverage) {
    _rootNode = createNewElement();
}

ParticleTreeElement* ParticleTree::createNewElement(unsigned char * octalCode) const {
    ParticleTreeElement* newElement = new ParticleTreeElement(octalCode); 
    return newElement;
}

