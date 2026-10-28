// SPDX-License-Identifier: Apache-2.0
// Runs the installed, pinned upstream implementation; no solver is copied here.
const fs = require('node:fs');
const path = require('node:path');
const { createRequire } = require('node:module');
const requireRef = createRequire(path.resolve(process.argv[2], 'package.json'));
const THREE = requireRef('three');
const {
  VRMSpringBoneJoint, VRMSpringBoneManager, VRMSpringBoneCollider,
  VRMSpringBoneColliderShapeSphere, VRMSpringBoneColliderShapeCapsule,
} = requireRef('@pixiv/three-vrm-springbone');
const input = JSON.parse(fs.readFileSync(path.join(__dirname, 'cases.json'), 'utf8'));
const rows = ['case,frame,joint,x,y,z,qw,qx,qy,qz'];
for (const spec of input.cases) {
  const scene = new THREE.Object3D();
  const nodes = spec.parents.map(() => new THREE.Object3D());
  nodes.forEach((n, i) => {
    n.position.fromArray(spec.translations[i]);
    (spec.parents[i] < 0 ? scene : nodes[spec.parents[i]]).add(n);
  });
  const groups = [];
  if (spec.collider) {
    const c = spec.collider;
    const options = { offset: new THREE.Vector3().fromArray(c.offset), radius: c.radius };
    if (c.tail) options.tail = new THREE.Vector3().fromArray(c.tail);
    const collider = new VRMSpringBoneCollider(c.shape === 'sphere'
      ? new VRMSpringBoneColliderShapeSphere(options) : new VRMSpringBoneColliderShapeCapsule(options));
    nodes[0].add(collider);
    groups.push({ colliders: [collider] });
  }
  const manager = new VRMSpringBoneManager();
  const pairs = [];
  if (spec.version === 1) {
    spec.declaration.slice(0, -1).forEach((i, k) => pairs.push([i, nodes[spec.declaration[k + 1]]]));
  } else {
    const visit = i => {
      const children = spec.parents.flatMap((p, k) => p === i ? [k] : []);
      pairs.push([i, children.length ? nodes[children[0]] : null]);
      children.forEach(visit);
    };
    spec.declaration.forEach(visit);
  }
  const p = input.parameters;
  const joints = pairs.map(([i, child]) => {
    const j = new VRMSpringBoneJoint(nodes[i], child, {
      stiffness: p.stiffness, dragForce: p.dragForce, gravityPower: p.gravityPower,
      gravityDir: new THREE.Vector3().fromArray(p.gravityDirection), hitRadius: p.hitRadius,
    }, groups);
    if (spec.center !== undefined) j.center = nodes[spec.center];
    manager.addJoint(j);
    return j;
  });
  scene.updateMatrixWorld(true);
  manager.setInitState();
  for (let frame = 1; frame <= input.frames; ++frame) {
    scene.position.fromArray(input.translationPerFrame).multiplyScalar(frame);
    nodes[0].quaternion.setFromAxisAngle(new THREE.Vector3(0, 0, 1), input.radiansPerFrame * frame);
    scene.updateMatrixWorld(true);
    // Joint.update consumes current world matrices, including its child's.
    // Refresh descendants between joints; the manager's partial traversal in
    // 3.4.5 otherwise measures a child's stale world position as bone length.
    for (const joint of joints) {
      scene.updateWorldMatrix(true, true);
      joint.update(input.seconds);
    }
    pairs.forEach(([i], k) => {
      // Read the actual solver tail, not a second integration of its rotation.
      const tail = joints[k]._currentTail.clone();
      if (spec.center !== undefined) tail.applyMatrix4(nodes[spec.center].matrixWorld);
      const q = nodes[i].quaternion;
      rows.push([spec.name, frame, i, tail.x, tail.y, tail.z, q.w, q.x, q.y, q.z].join(','));
    });
  }
}
fs.writeFileSync(process.argv[3], rows.join('\n') + '\n');
