// SPDX-License-Identifier: Apache-2.0
// A rigid, unit-scale subset of Unity's value/transform API for the reference
// harness. All integration, topology traversal, state and collision code is
// the downloaded UniVRM implementation. No Unity native calls or engine needed.
using System;
using System.Collections;
using System.Collections.Generic;

namespace UnityEngine
{
    public struct Vector3
    {
        public float x, y, z;
        public Vector3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
        public static Vector3 zero => new Vector3();
        public float magnitude => (float)Math.Sqrt(SqrMagnitude(this));
        public Vector3 normalized => magnitude > 1e-8f ? this / magnitude : zero;
        public static float SqrMagnitude(Vector3 a) => a.x*a.x + a.y*a.y + a.z*a.z;
        public static float Dot(Vector3 a, Vector3 b) => a.x*b.x + a.y*b.y + a.z*b.z;
        public static Vector3 Cross(Vector3 a, Vector3 b) => new Vector3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.x+b.x, a.y+b.y, a.z+b.z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.x-b.x, a.y-b.y, a.z-b.z);
        public static Vector3 operator *(Vector3 a, float b) => new Vector3(a.x*b, a.y*b, a.z*b);
        public static Vector3 operator /(Vector3 a, float b) => a * (1/b);
    }
    public struct Quaternion
    {
        public float x, y, z, w;
        public Quaternion(float x, float y, float z, float w) { this.x=x; this.y=y; this.z=z; this.w=w; }
        public static Quaternion identity => new Quaternion(0,0,0,1);
        public static Quaternion Inverse(Quaternion q) => new Quaternion(-q.x,-q.y,-q.z,q.w);
        public static Quaternion operator *(Quaternion a, Quaternion b) => new Quaternion(
            a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y, a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w, a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z);
        public static Vector3 operator *(Quaternion q, Vector3 v)
        {
            var c = new Vector3(q.x,q.y,q.z);
            return v + Vector3.Cross(c, Vector3.Cross(c,v) + v*q.w)*2;
        }
        public static Quaternion FromToRotation(Vector3 a, Vector3 b)
        {
            a=a.normalized; b=b.normalized;
            float w=1+Vector3.Dot(a,b);
            var v=Vector3.Cross(a,b);
            if (w < 1e-6f) { v=Vector3.Cross(a,Math.Abs(a.x)<0.9f ? new Vector3(1,0,0) : new Vector3(0,1,0)).normalized; return new Quaternion(v.x,v.y,v.z,0); }
            float norm=(float)Math.Sqrt(w*w+Vector3.SqrMagnitude(v));
            return new Quaternion(v.x/norm,v.y/norm,v.z/norm,w/norm);
        }
    }
    public class Transform : IEnumerable<Transform>
    {
        public Transform parent;
        public Vector3 localPosition;
        public Quaternion localRotation = Quaternion.identity;
        public List<Transform> children = new List<Transform>();
        public Transform transform => this;
        public int childCount => children.Count;
        public Vector3 lossyScale => new Vector3(1,1,1);
        public Vector3 position => parent == null ? localPosition : parent.TransformPoint(localPosition);
        public Quaternion rotation
        {
            get => parent == null ? localRotation : parent.rotation*localRotation;
            set => localRotation = parent == null ? value : Quaternion.Inverse(parent.rotation)*value;
        }
        public Vector3 TransformPoint(Vector3 p) => position + rotation*p;
        public Vector3 InverseTransformPoint(Vector3 p) => Quaternion.Inverse(rotation)*(p-position);
        public Matrix4x4 worldToLocalMatrix => new Matrix4x4(this);
        public Transform GetChild(int i) => children[i];
        public IEnumerable<T> GetComponentsInChildren<T>(bool active)
        {
            yield return (T)(object)this;
            foreach(var child in children) foreach(var t in child.GetComponentsInChildren<T>(active)) yield return t;
        }
        public IEnumerator<Transform> GetEnumerator() => children.GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
    public struct Matrix4x4
    {
        Transform target;
        public Matrix4x4(Transform t) { target=t; }
        public Vector3 MultiplyPoint(Vector3 p) => target.InverseTransformPoint(p);
    }
    public struct Color { public static Color gray => new Color(); }
    public static class Gizmos
    {
        public static Color color;
        public static void DrawLine(Vector3 a, Vector3 b) { }
        public static void DrawWireSphere(Vector3 a, float b) { }
    }
    public static class Mathf { public static float Max(float a,float b) => Math.Max(a,b); }
}
namespace UniGLTF
{
    public static class TransformExtensions
    {
        public static float UniformedLossyScale(this UnityEngine.Transform t) => 1;
    }
}
namespace VRM
{
    public class VRMSpringBoneColliderGroup
    {
        public UnityEngine.Transform transform;
        public SphereCollider[] Colliders;
        public struct SphereCollider { public UnityEngine.Vector3 Offset; public float Radius; }
    }
}
