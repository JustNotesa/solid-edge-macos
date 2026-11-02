// Gibt Enums und Schnittstellen (Methoden/Eigenschaften mit Parametern) einer Typbibliothek als Text aus.
// Aufruf: tlbdump.exe <datei.tlb> > ausgabe.txt   (Nachschlagen der Solid-Edge-API ohne SDK)
using System;
using System.Runtime.InteropServices;
using TYPEDESC = System.Runtime.InteropServices.ComTypes.TYPEDESC;
using TYPEATTR = System.Runtime.InteropServices.ComTypes.TYPEATTR;
using TYPEKIND = System.Runtime.InteropServices.ComTypes.TYPEKIND;
using VARDESC = System.Runtime.InteropServices.ComTypes.VARDESC;
using FUNCDESC = System.Runtime.InteropServices.ComTypes.FUNCDESC;
using ELEMDESC = System.Runtime.InteropServices.ComTypes.ELEMDESC;
using INVOKEKIND = System.Runtime.InteropServices.ComTypes.INVOKEKIND;
using PARAMFLAG = System.Runtime.InteropServices.ComTypes.PARAMFLAG;
using System.Runtime.InteropServices.ComTypes;

class TlbDump
{
    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode)]
    static extern int LoadTypeLibEx(string file, int regKind, out ITypeLib lib);

    static string Name(ITypeInfo ti, int memid)
    {
        string n, d, h; int c;
        ti.GetDocumentation(memid, out n, out d, out c, out h);
        return n;
    }

    static string VtName(ITypeInfo ti, TYPEDESC td)
    {
        VarEnum vt = (VarEnum)td.vt;
        if (vt == VarEnum.VT_PTR) return VtName(ti, (TYPEDESC)Marshal.PtrToStructure(td.lpValue, typeof(TYPEDESC))) + "*";
        if (vt == VarEnum.VT_SAFEARRAY) return "safearray(" + VtName(ti, (TYPEDESC)Marshal.PtrToStructure(td.lpValue, typeof(TYPEDESC))) + ")";
        if (vt == VarEnum.VT_USERDEFINED) {
            ITypeInfo r; ti.GetRefTypeInfo((int)td.lpValue.ToInt64(), out r);
            return Name(r, -1);
        }
        return vt.ToString().Replace("VT_", "").ToLower();
    }

    static void Main(string[] a)
    {
        ITypeLib lib;
        int hr = LoadTypeLibEx(a[0], 0, out lib);
        if (hr != 0) { Console.Error.WriteLine("LoadTypeLibEx: 0x" + hr.ToString("x")); return; }
        int n = lib.GetTypeInfoCount();
        for (int i = 0; i < n; i++) {
            ITypeInfo ti; lib.GetTypeInfo(i, out ti);
            IntPtr pa; ti.GetTypeAttr(out pa);
            TYPEATTR ta = (TYPEATTR)Marshal.PtrToStructure(pa, typeof(TYPEATTR));
            string tn = Name(ti, -1);
            Console.WriteLine("== " + ta.typekind + " " + tn + " {" + ta.guid + "} flags=" + ta.wTypeFlags);
            if (ta.typekind == TYPEKIND.TKIND_ENUM) {
                for (int v = 0; v < ta.cVars; v++) {
                    IntPtr pv; ti.GetVarDesc(v, out pv);
                    VARDESC vd = (VARDESC)Marshal.PtrToStructure(pv, typeof(VARDESC));
                    object val = Marshal.GetObjectForNativeVariant(vd.desc.lpvarValue);
                    Console.WriteLine("  " + Name(ti, vd.memid) + " = " + val);
                    ti.ReleaseVarDesc(pv);
                }
            } else if (ta.typekind == TYPEKIND.TKIND_DISPATCH || ta.typekind == TYPEKIND.TKIND_INTERFACE) {
                for (int f = 0; f < ta.cFuncs; f++) {
                    IntPtr pf; ti.GetFuncDesc(f, out pf);
                    FUNCDESC fd = (FUNCDESC)Marshal.PtrToStructure(pf, typeof(FUNCDESC));
                    string[] names = new string[fd.cParams + 1]; int cn;
                    ti.GetNames(fd.memid, names, names.Length, out cn);
                    string kind = fd.invkind == INVOKEKIND.INVOKE_FUNC ? "" : fd.invkind == INVOKEKIND.INVOKE_PROPERTYGET ? "get " : "put ";
                    var ps = new System.Text.StringBuilder();
                    int sz = Marshal.SizeOf(typeof(ELEMDESC));
                    for (int p = 0; p < fd.cParams; p++) {
                        ELEMDESC ed = (ELEMDESC)Marshal.PtrToStructure(new IntPtr(fd.lprgelemdescParam.ToInt64() + p * sz), typeof(ELEMDESC));
                        bool opt = (ed.desc.paramdesc.wParamFlags & PARAMFLAG.PARAMFLAG_FOPT) != 0;
                        bool outp = (ed.desc.paramdesc.wParamFlags & PARAMFLAG.PARAMFLAG_FOUT) != 0;
                        if (p > 0) ps.Append(", ");
                        ps.Append((opt ? "[opt] " : "") + (outp ? "[out] " : "") + VtName(ti, ed.tdesc) + " " + (p + 1 < cn ? names[p + 1] : "?"));
                    }
                    Console.WriteLine("  " + kind + names[0] + "(" + ps + ") -> " + VtName(ti, fd.elemdescFunc.tdesc));
                    ti.ReleaseFuncDesc(pf);
                }
            }
            ti.ReleaseTypeAttr(pa);
        }
    }
}
