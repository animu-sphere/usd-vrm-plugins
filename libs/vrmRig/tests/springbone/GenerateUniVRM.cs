// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Web.Script.Serialization;
using UnityEngine;
using VRM;
using VRM.SpringBone;

class GenerateUniVRM
{
    static Dictionary<string,object> Obj(object o) => (Dictionary<string,object>)o;
    static object[] Arr(object o) => ((System.Collections.IEnumerable)o).Cast<object>().ToArray();
    static float F(object o) => Convert.ToSingle(o);
    static Vector3 Vec(object o) { var a=Arr(o); return new Vector3(F(a[0]),F(a[1]),F(a[2])); }
    static void Main(string[] args)
    {
        try { Run(args); }
        catch (Exception e) { Console.Error.WriteLine(e.GetType().FullName); Console.Error.WriteLine(e.Message); Console.Error.WriteLine(e.StackTrace); Environment.Exit(1); }
    }
    static void Run(string[] args)
    {
        System.Threading.Thread.CurrentThread.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        var input = new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(args[0]));
        var rows = new List<string> { "case,frame,joint,x,y,z,qw,qx,qy,qz" };
        var p=Obj(input["parameters"]);
        foreach(var item in Arr(input["cases"]))
        {
            var spec=Obj(item);
            if(Convert.ToInt32(spec["version"]) != 0) continue;
            var scene = new Transform();
            var parents=Arr(spec["parents"]);
            var nodes=parents.Select(_ => new Transform()).ToArray();
            var translations=Arr(spec["translations"]);
            for(int i=0;i<nodes.Length;++i)
            {
                nodes[i].localPosition=Vec(translations[i]);
                int parent=Convert.ToInt32(parents[i]);
                nodes[i].parent=parent<0 ? scene : nodes[parent];
                nodes[i].parent.children.Add(nodes[i]);
            }
            var groups=new VRMSpringBoneColliderGroup[0];
            if(spec.ContainsKey("collider"))
            {
                var c=Obj(spec["collider"]);
                if((string)c["shape"] != "sphere") throw new Exception("VRM 0.x reference supports spheres only");
                groups=new[] { new VRMSpringBoneColliderGroup { transform=nodes[0], Colliders=new[] {
                    new VRMSpringBoneColliderGroup.SphereCollider { Offset=Vec(c["offset"]), Radius=F(c["radius"]) }
                } } };
            }
            Transform center=spec.ContainsKey("center") ? nodes[Convert.ToInt32(spec["center"])] : null;
            var sceneInfo=new SceneInfo(Arr(spec["declaration"]).Select(i => nodes[Convert.ToInt32(i)]).ToArray(), center, groups, Vector3.zero);
            var settings=new SpringBoneSettings(F(p["stiffness"]),F(p["dragForce"]),Vec(p["gravityDirection"]),F(p["gravityPower"]),F(p["hitRadius"]),false);
            var system=new SpringBoneSystem();
            system.Setup(sceneInfo,true);
            // Inspect the actual upstream state rather than deriving tails in the harness.
            var stateField=typeof(SpringBoneSystem).GetField("m_joints",BindingFlags.NonPublic|BindingFlags.Instance);
            for(int frame=1; frame<=Convert.ToInt32(input["frames"]); ++frame)
            {
                scene.localPosition=Vec(input["translationPerFrame"])*frame;
                double angle=F(input["radiansPerFrame"])*frame/2.0;
                nodes[0].localRotation=new Quaternion(0,0,(float)Math.Sin(angle),(float)Math.Cos(angle));
                system.UpdateProcess(F(input["seconds"]),sceneInfo,settings);
                var states=(List<(Transform,SpringBoneJointInit,SpringBoneJointState)>)stateField.GetValue(system);
                foreach(var entry in states)
                {
                    int i=Array.IndexOf(nodes,entry.Item1);
                    var tail=entry.Item3.ToWorld(center).CurrentTail;
                    var q=nodes[i].localRotation;
                    rows.Add(string.Join(",",new object[] {spec["name"],frame,i,tail.x.ToString("R"),tail.y.ToString("R"),tail.z.ToString("R"),q.w.ToString("R"),q.x.ToString("R"),q.y.ToString("R"),q.z.ToString("R")}));
                }
            }
        }
        File.WriteAllLines(args[1],rows);
    }
}
